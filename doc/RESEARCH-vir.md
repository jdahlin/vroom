# VIR: what LLVM IR got wrong, and what other compilers did instead

Research of 2026-11-11 for `PLAN-vir.md`. Sources: three web research passes
(papers, LLVM issues and RFCs, ClangBuiltLinux, LKML, engineering blogs) and
one pass over `src/codegen/`. Points the research could not confirm on the
page are marked *(unverified)*. Each item ends with the rule VIR takes from it;
the rules are collected in section 5.

LLVM's own lead maintainer frames the subject: the IR is well designed overall,
but core mistakes become "firmly embedded in the code base" (Popov 2021 [P21]),
and his 2026 list is "opportunities to improve", not reasons to avoid LLVM
[P26]. The same applies here: this is a list of things that are cheap to get
right on day one and expensive later.

## 1. Semantics

### 1.1 undef, poison, freeze

- `undef` may be a different value at each use. `x + x` is not guaranteed even,
  and `2*y -> y+y` is unsound [U26].
- undef and poison conflict: loop unswitching needs branch-on-poison to be
  defined, GVN needs it to be UB; both cannot be sound (Lee et al., "Taming
  Undefined Behavior in LLVM", PLDI 2017 [T17], *(unverified this pass)*). The
  paper proposed `freeze` and removing undef.
- Branch on undef/poison is UB, but `select` is not, so
  `select x, y, false -> and x, y` is wrong when `y` is poison. Alive2 found
  this to be the bulk of failures on real programs [A21].
- undef survives because uninitialized memory still loads as undef, which
  bit-field load-modify-store and raw copies depend on. LLVM 10 to 21 removed
  58 % of undef uses in LLVM and 66 % in Clang, then stalled [U26].
- Alive2 still finds 136 miscompilations in LLVM's unit tests; 25 are unsound
  only because undef exists [U26].
- The fix under way is a **byte type** (`b<N>`: raw memory with per-bit poison
  and possibly pointer bits). RFC 2021 stalled; new RFC January 2026 [B26]; a
  first patch landed and was reverted in March 2026; trunk LangRef has a "Byte
  Type" section. Whether Clang emits it by default is *(unverified)*.

**VIR:** no undef and no poison. Every value is a concrete bit pattern, memory
is concrete bytes, an uninitialized local reads as 0, uninitialized memory
holds arbitrary but fixed bytes. Strictness flags mean immediate UB at the
flagged instruction (1.6).

### 1.2 Pointer provenance

- `inttoptr(ptrtoint p) -> p` is unsound. LLVM issue #33896 (Bugzilla 34548),
  open since September 2017; reported in October 2025 as miscompiling
  real-world Rust (rust-lang/rust#147265) [I33896].
- Jung's example [J20]: integer equality substitution, round-trip removal and
  alias-based forwarding are each plausible and together wrong:
  ```c
  char p[1], q[1] = {0};
  uintptr_t ip = (uintptr_t)(p+1), iq = (uintptr_t)q;
  if (iq == ip) { *(char*)iq = 10; print(q[0]); }   /* can print 0 */
  ```
  The round-trip removal is the one that must go.
- `p == q` does not license replacing `*p` by `*q` (one past the end, freed and
  reallocated). Lee et al., "Reconciling High-Level Optimizations and Low-Level
  Code in LLVM", OOPSLA 2018 [R18]. SCCP got a narrow fix in 2020; the broader
  GVN fix was still in review in 2023, final state *(unverified)*.
- 2025: RFC "Clarifying the semantics of ptrtoint" led to `ptrtoaddr` (address
  only, no inverse) [PT25]. LangRef now separates provenance capture from
  address capture, without a formal model.
- C side: ISO/IEC TS 6010:2025 defines a provenance-aware object model for C.

**VIR:** `ptr` is its own type. `p2i` exposes the object, `i2p` may point to
any exposed object, round trips are never folded, a pointer is only replaced by
the same SSA value. Comparisons and `psub` read the address without exposing.

### 1.3 Memory model

- memcpy lowered to an integer load and store drops provenance and spreads
  poison over bytes [B26].
- **Lifetime markers vs address identity:** issue #45725, open since 2020:
  `&a == &b` folds to false for distinct allocas while stack colouring gives
  them the same address [I45725]. The marker semantics were still being
  rewritten in 2025-26 (markers only on allocas; load outside the lifetime is
  poison, store is UB; double `lifetime.start` in review) *(from commit mail)*.
- **restrict:** only function-argument `noalias` is native; inlining turns it
  into scoped metadata, with unsoundness fixed in 2021, 2024 and 2025. The
  "full restrict" patch set (RFC 2019) was last updated in 2022 and is not
  merged *(unverified)* [FR19].
- **TBAA:** metadata only; unions an open design problem since a 2017 RFC; the
  new struct-path format miscompiled `std::string` (#55384, fixed 2022).
- **Null:** `null_pointer_is_valid` is a per-function attribute that every
  fold must remember to check.

**VIR:** no lifetime markers: a slot has a scope, sharing frame space is a
lowering decision, comparisons of distinct slot addresses are never folded.
One alias rule in 1.0 (escaped or not). Type tags are a reserved field on
loads and stores, never metadata.

### 1.4 Control and effects

- LLVM assumed forward progress for every language until LLVM 12
  (`mustprogress`, `willreturn`), miscompiling C and Rust `loop {}`.
- **setjmp:** issue #116668 (2024): GVN forwarded a store across a
  `returns_twice` call; fixed in alias analysis. The CFG has no longjmp edges,
  and correctness depends on the front end marking every such declaration.
- **Computed goto:** `indirectbr` successors cannot be redirected, critical
  edges cannot be split, the block cannot be cloned; the October 2025 RFC calls
  miscompiles frequent and slow to surface [IB25]. Inlining is blocked when a
  `blockaddress` has other users.
- `unreachable` has no defined behaviour, and code after it can run off the
  end of the function (3.5).

**VIR:** loops and calls may diverge unless the callee is `pure`/`const`.
`ibr` carries a closed list of the function's address-taken labels, and those
blocks take no parameters. 1.0 does not build VIR for a function that calls a
`returns_twice` function. `unreachable` always lowers to a trap.

### 1.5 Floating point

- Strict FP is a parallel set of `constrained` intrinsics. The RFC "Yet another
  strict FP" (Popov, May 2026) lists duplication, no static rounding modes and
  no per-instruction denormal mode, and proposes FP-environment memory
  locations; unsettled [FP26].
- NaN results were unspecified until 2023 (sign non-deterministic, payload
  propagated or preferred).
- Fast-math flags yield poison (`nnan`, `ninf`) or nondeterminism (`nsz`);
  Alive2 shows `fadd (fmul nsz a, b), +0.0 -> fmul nsz a, b` is not a
  refinement [A21].

**VIR:** one set of FP operations and a per-function `fenv` flag that turns
them into effects. The constant folder never produces a NaN. No fast-math
flags in 1.0.

### 1.6 Several encodings of one fact

Non-negativity or no-overflow can be a poison flag, metadata, an attribute or
an assume. Metadata is lost too often and assumes are kept too often [P26].

**VIR:** a fact is an instruction flag bit or field, with one meaning:
violating it is immediate UB at that instruction. A pass that moves or
speculates a flagged instruction clears the flag. This avoids a deferred-UB
value altogether; the cost is that flagged arithmetic cannot be hoisted with
its flag.

### 1.7 No specification

Alive2 (PLDI 2021) found 121 refinement violations in LLVM's own unit tests:
43 wrong with undef input, 18 introducing a branch on undef/poison, 17 memory,
9 vectors, 5 select, the rest smaller classes [A21]. A formal specification
working group was only formed recently [P26].

**VIR:** an interpreter is the reference semantics and exists before the first
optimization. Every pass is checked by interpreting before and after.

## 2. Structure and compile time

### 2.1 In-memory representation

- Every instruction is a heap object in a linked list with bidirectional
  use-lists. WebKit's B3 dropped use-lists and replace-all-uses, and LLVM took
  4.7x longer to compile the same code [B3]. Umbra IR keeps no user lists
  either; in that study building LLVM IR is mostly allocation cost, and
  destroying the module alone is about 1 % of cheap-mode compile time [E24].
- Constants and types are uniqued in `LLVMContext`: `i32 0` is shared by every
  function and its use-list is context-global, the central obstacle to parallel
  function passes. The RFC to remove such use-lists is from 2016; a patch was
  still in progress in 2025 [CD16]. Context-owned types also cannot see the
  data layout [P26].
- Phi nodes must lead the block, execute atomically (lost-copy bugs), and
  function arguments need a separate node kind (MLIR rationale [MLIR]).
- Debug info lived in the instruction stream as `dbg.value` calls. Moving it
  to records gained about 4 % compile time and needed an API break [RD].
- Typed pointers forced bitcasts everywhere (fixed by opaque pointers);
  type-based GEP must be decomposed to byte offsets, a couple of percent of
  compile time (`ptradd` migration under way); constant expressions were a
  second expression language folded without a data layout (mostly removed)
  [P21][P26].

**VIR:** flat array of 16-byte instructions, 32-bit operand indices, no
use-lists (use counts by a sweep), constants as per-function instructions,
block parameters, source locations in a side array, byte-offset `ptradd`,
constants limited to numbers and symbol plus offset.

### 2.2 Where -O0 time goes

- Engelke and Schwarz, CGO 2024 [E24], LLVM "cheap" pipeline on x86-64: 67
  passes; AsmPrinter/MC 12 % plus 3 % fixups (a second in-memory encoding of
  every instruction, virtual calls, string-hashed local labels); legacy pass
  manager bookkeeping 5 %; prologue/epilogue 4 %; FastISel fallbacks to
  SelectionDAG 36 % of selection time; passes that scan for constructs that
  never occur. GlobalISel at -O0 on AArch64 is 2.7x slower than FastISel.
  Their conclusion: an order of magnitude needs a different architecture.
- TPDE [TPDE]: one analysis pass plus one codegen pass over LLVM IR is 8-24x
  faster than LLVM -O0 (geomean 18.96x on AArch64), runtime within 9 %.
- In the same study Cranelift is only 20-35 % faster than LLVM-cheap, and
  register allocation is its largest phase [E24].

**VIR:** `-O0` stays AST-direct. At `-O1` the cost model is the number of
sweeps over the array; the pass list is fixed, with no pass manager; the
register allocator has its own time budget.

### 2.3 Levels

LLVM lowers through four representations (IR, SelectionDAG, MachineInstr, MC).
SelectionDAG is per basic block and shares no code with FastISel; GlobalISel is
about ten years old and no target that started on SelectionDAG has fully moved
[P26]. B3 replaced the last three with one (Air); Cranelift replaced its
legaliser by a single backward lowering sweep into a flat array [CL20].

**VIR:** two levels: VIR, then the existing 16-byte machine records
(`codegen/rec/`).
Legalisation is part of the lowering sweep.

### 2.4 Things to avoid

- Sea of Nodes: V8 left it in 2025; the CFG-based replacement roughly halved
  compile time, and Sea of Nodes had about 3x the L1 cache misses [V8].
- LICM that always hoists and relies on a backend to sink again, which mostly
  does not happen [P26].

## 3. The C front-end interface

### 3.1 ABI lowering

The target's C ABI is the front end's job in LLVM, and the contract is
undocumented. IR types cannot express unions or type alignment, and
`__int128` and `_BitInt(128)` are both `i128` with different ABIs (RFC,
February 2025 [ABI25]). `i128` alignment on x86 was only fixed in LLVM 18 and
forced a Rust layout change. The proposed ABI lowering library covers BPF and
x86-64 Linux in its 2025 prototype. QBE instead passes aggregate types in the
IL and classifies in the backend.

**VIR:** a call carries its C function type; the target classifies it
(`LO(classify)`). Aggregates are never IR values.

### 3.2 Varargs

The `va_arg` instruction cannot take aggregates and knows nothing about
indirect passing, so clang open-codes `va_list` on most targets [VA17].

**VIR:** `va_start`/`va_arg`/`va_copy` carry the C type and are lowered by the
target.

### 3.3 Inline assembly

- Constraint alternatives are fixed at instruction selection: "rm" is treated
  as "m". Issue #20571, open since 2014 [I20571].
- asm goto with outputs: clang 11 for the fallthrough edge only, indirect
  edges later; the kernel disables it before clang 17.
- Jumps across `__attribute__((cleanup))` scopes are rejected by clang and
  still hit kernel `guard()` code in 2026.
- LLVM emits a BTI landing pad at every asm-goto label, GCC does not (RFC,
  April 2025).

**VIR:** the asm operation keeps the constraint string and the allocator picks
the alternative. `asmgoto` is a terminator with ordinary, splittable edges;
its outputs are results of the terminator.

### 3.4 `__builtin_constant_p`

It must be resolved after inlining (`llvm.is.constant`). The kernel broke each
time the fold point moved: PR41459 (address of a global treated as constant),
ClangBuiltLinux #1310 (true for a loop induction variable).

**VIR:** an `isconst` operation resolved at one documented point, after inline
expansion and the simplify pass.

### 3.5 Kernel requirements

- objtool reports code that "falls through to next function" when an
  unreachable switch default becomes a jump off the function end
  (ClangBuiltLinux #679, #1440, again #2130 in 2026).
- The kernel's flags make every strictness fact switchable: wrapping overflow,
  no type-based aliasing, null as a valid address.
- Prologue features must compose: patchable entries with kCFI hashes, BTI,
  PAC, shadow call stack, the stack protector's per-task canary.
- Stack frame size under sanitizers has been an open ClangBuiltLinux issue
  (#39) since 2018.

**VIR:** `unreachable` lowers to `brk`. Strictness is per instruction and off
by default. The prologue stays in `finish_function`, outside VIR.

### 3.6 Data

Volatile bit-fields must be accessed at the container width (AAPCS); clang was
non-compliant until 2020. `_BitInt` returned a miscompile in 2026 from an IR
type and memory type that differ.

**VIR:** loads and stores have an explicit width chosen by `cc`'s layout, and
`volatile` pins the access.

### 3.7 WebAssembly

LLVM repairs irreducible control flow after the fact with a dispatch block,
then stackifies; the pass's own comments call the result inefficient in many
cases. Ramsey's "Beyond Relooper" (ICFP 2022) gives a dominator-tree algorithm.

**VIR:** out of scope for 1.0. Blocks are kept in reverse postorder with loop
headers marked, which that algorithm needs.

## 4. What other compilers chose

| Compiler | Choice | Measured |
|---|---|---|
| WebKit B3/Air | No use-lists, value and statement one object, blocks hold vectors, two levels | LLVM 4.7x slower to compile [B3] |
| Cranelift | Entity indices, block parameters, no aggregate types, backward lowering, regalloc2 | regalloc2 about 20 % faster overall; a struct growing from 16 to 24 bytes was measurably slower [RA2] |
| Zig | Tag array, payload array, `u32` extra array; codegen parallel per function | Self-hosted x86_64 vs LLVM: compiler self-build 75 s to 20 s, then 10 s threaded [ZIG] |
| Go SSA | Dense ids, rewrite rules in a DSL, opcodes lowered in place, memory as an SSA value | Go 1.7: 5-35 % less CPU time in benchmarks [GO] |
| V8 Turboshaft | CFG, each phase copies the graph into a new one | Compile time roughly halved vs Sea of Nodes [V8] |
| LuaJIT | 64-bit instructions in one array, constants growing downward, fold and CSE at append | — |
| QBE | Four value types, C ABI in the IL | Under 8 kloc |
| Umbra IR | One buffer, constants as instructions, a scratch slot per instruction | 6,678 functions in 64 ms [E24] |
| Makarov's MIR | No SSA on the short pipeline | About 178x faster than GCC -O2 on one benchmark, about 94 % of its speed [MIR] |
| TPDE | Analysis sweep plus one codegen sweep | 8-24x faster than LLVM -O0 [TPDE] |
| Braun et al. 2013 | SSA built directly from the AST, no dominance analysis first | *(abstract only read)* [BR13] |

## 5. The rules VIR takes

1. No undef, no poison; values and memory are concrete.
2. Strictness is an instruction flag meaning immediate UB; moving the
   instruction clears it.
3. `ptr` is not an integer; exposure is explicit; round trips are not folded.
4. Slots have scopes, not lifetime markers; slot address comparisons are not
   folded.
5. One alias rule in 1.0; type tags are a field, never metadata.
6. Effects are a static property of the opcode, refined by callee attributes.
7. The interpreter is the specification and comes before the first pass.
8. Flat 16-byte instructions, 32-bit indices, no use-lists.
9. Nothing mutable is shared between functions.
10. Block parameters, not phi nodes.
11. Source locations in a side array.
12. Two levels: VIR and the machine records.
13. SSA is built on the fly, as the walker emits operations.
14. A fixed pass list; no pass manager.
15. Calls carry the C function type; the target classifies.
16. Inline asm keeps its constraints until register allocation.
17. `isconst` has one resolution point.
18. `unreachable` is a trap.
19. Memory accesses have explicit width, alignment and volatility.
20. One set of FP operations plus a function flag.

## 6. What the emitter looks like today (2026-11-11, `main`)

- AST nodes are 24 bytes, 32-bit indices into per-TU arrays; conversions are
  explicit `N_CAST` nodes; constants are evaluated on demand by `cc_eval`.
- `codegen/prepass.c` computes per function: weighted use counts (`luse`), locals
  that need memory (`lnoreg`), single-constant variables, label flags.
- Three generators: arm64 (`arch/a64/codegen/`, its own AST generator), x86-64
  (the shared walker `codegen/walk/` calling `arch/x64/codegen/` through `LO()`),
  wasm (`arch/wasm/codegen/`, its own).
- `LO()` is about 70 operations on `val`/`lval` with physical registers the
  CPU hands out (`treg`/`tfree`). No target has a register allocator: values
  are spilled around calls, and a function is generated again when temporaries
  run out.
- Machine records are 16 bytes (`codegen/rec/rec.h`, arm64's `ins.h`) with a
  per-CPU op table (`opdesc`). Shared pieces without SSA: algebra rules in
  `codegen/opt/`, record passes in `codegen/rec/`, block graph and bounded liveness
  in `codegen/flow/` (`PLAN-fas2.md`, OPT0-OPT8).
- `PLAN-x86.md` ("What cannot be done without SSA") names what this leaves out:
  GVN, LICM and real register allocation, to be done by an optional SSA pass
  with virtual registers at `-O2`. VIR is that pass.
- arm64, static instruction count on 80 kernel TUs: 1.51x clang -O2; clang's
  backend with mem2reg/SROA alone gives 1.15x (`PLAN-fas2.md`, status 2.6).

*(The first survey for this document was made on a branch 566 commits behind
`main`, without x86-64; the list above was corrected against `origin/main`.
Line references from that survey are not reproduced here.)*

## Sources

- [P21] https://www.npopov.com/2021/06/02/Design-issues-in-LLVM-IR.html
- [P26] https://www.npopov.com/2026/01/11/LLVM-The-bad-parts.html
- [T17] Lee et al., "Taming Undefined Behavior in LLVM", PLDI 2017; summary: https://blog.regehr.org/?p=1496
- [U26] Lobo et al., "Towards Removing Undef Values from LLVM IR", PLDI 2026: https://pldi26.sigplan.org/details/pldi-2026-papers/7/Towards-Removing-Undef-Values-from-LLVM-IR
- [B26] https://discourse.llvm.org/t/rfc-add-a-new-byte-type-to-llvm-ir/89522 and https://web.ist.utl.pt/nuno.lopes/pubs/byte-type-pldi26.pdf
- [A21] Lopes et al., "Alive2: Bounded Translation Validation for LLVM", PLDI 2021: https://www-old.cs.utah.edu/~regehr/alive2-pldi21.pdf
- [I33896] https://github.com/llvm/llvm-project/issues/33896
- [J20] https://www.ralfj.de/blog/2020/12/14/provenance.html
- [R18] https://users.cs.utah.edu/~regehr/oopsla18.pdf
- [PT25] https://discourse.llvm.org/t/rfc-clarifying-the-semantics-of-ptrtoint/83987
- [I45725] https://github.com/llvm/llvm-project/issues/45725
- [FR19] https://lists.llvm.org/pipermail/llvm-dev/2019-March/131127.html and https://discourse.llvm.org/t/aa-ptr-provenance-full-restrict-llvm-dev-round-table-summary/66722
- TBAA: https://discourse.llvm.org/t/rfc-representing-unions-in-tbaa/43951, https://github.com/llvm/llvm-project/issues/55384
- setjmp: https://github.com/llvm/llvm-project/issues/116668
- [IB25] https://discourse.llvm.org/t/rfc-changing-the-indirectbr-blockaddress-representation/88677
- [FP26] https://discourse.llvm.org/t/rfc-yet-another-strict-fp/90798
- LangRef: https://llvm.org/doc/LangRef.html
- [B3] https://webkit.org/blog/5852/introducing-the-b3-jit-compiler/
- [E24] Engelke and Schwarz, CGO 2024: https://home.cit.tum.de/~engelke/pubs/2403-cgo.pdf
- [TPDE] https://arxiv.org/abs/2505.22610
- [CD16] https://discourse.llvm.org/t/rfc-constantdata-should-not-have-use-lists/42606
- [MLIR] https://mlir.llvm.org/doc/Rationale/Rationale/
- [RD] https://llvm.org/doc/RemoveDIsDebugInfo.html
- Compile time: https://www.npopov.com/2020/05/10/Make-LLVM-fast-again.html, https://www.npopov.com/2026/01/31/This-year-in-LLVM-2025.html
- GlobalISel: https://discourse.llvm.org/t/instruction-selection-globalisel-vs-selectiondag-in-2025/87602
- [CL20] https://cfallin.org/blog/2020/09/18/cranelift-isel-1/
- [RA2] https://cfallin.org/blog/2022/06/09/cranelift-regalloc2/
- Cranelift vs LLVM: https://github.com/bytecodealliance/wasmtime/blob/main/cranelift/doc/compare-llvm.md
- [ZIG] https://github.com/ziglang/zig/issues/16270, https://ziglang.org/devlog/2025/
- [GO] https://github.com/golang/go/blob/master/src/cmd/compile/internal/ssa/README.md, https://go.dev/doc/go1.7
- [V8] https://v8.dev/blog/leaving-the-sea-of-nodes
- LuaJIT IR: https://github.com/tarantool/tarantool/wiki/LuaJIT-SSA-IR
- QBE: https://c9x.me/compile/doc/il.html, https://c9x.me/compile/doc/llvm.html
- [MIR] https://developers.redhat.com/blog/2020/01/20/mir-a-lightweight-jit-compiler-project
- [BR13] https://c9x.me/compile/bib/braun13cc.pdf
- [ABI25] https://discourse.llvm.org/t/rfc-an-abi-lowering-library-for-llvm/84495, https://blog.llvm.org/posts/2025-08-25-abi-library/
- Explicit calling convention RFC: https://lists.llvm.org/pipermail/llvm-dev/2019-January/129143.html
- i128: https://blog.rust-lang.org/2024/03/30/i128-layout-update/
- [VA17] https://lists.llvm.org/pipermail/llvm-dev/2017-August/116337.html
- [I20571] https://github.com/llvm/llvm-project/issues/20571
- asm goto: https://discourse.llvm.org/t/rfc-syncing-asm-goto-with-outputs-with-gcc/65453, https://discourse.llvm.org/t/rfc-asm-goto-vs-branch-target-enforcement/85845
- `__builtin_constant_p`: https://llvm.org/pr41459, https://github.com/ClangBuiltLinux/linux/issues/1310
- objtool: https://github.com/ClangBuiltLinux/linux/issues/679, https://lore.kernel.org/all/20210420225550.szbmrpwv6awd7tp5@treble/
- Frame size: https://github.com/ClangBuiltLinux/linux/issues/39
- kCFI: https://lwn.net/Articles/893164/
- Wasm control flow: https://llvm.org/doc/doxygen/WebAssemblyFixIrreducibleControlFlow_8cpp_source.html, https://icfp22.sigplan.org/details/icfp-2022-papers/1/Beyond-Relooper-Recursive-Translation-of-Unstructured-Control-Flow-to-Structured-Con
