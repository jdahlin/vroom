# x86-64 Linux as a target: a new arch in vcc

**Goal:** `vcc --target=x86_64-linux-gnu -c x.c` produces an ELF object that
links against glibc and runs with the same result as clang's. `-S` produces
AT&T syntax like clang's.  The final goal is an x86-64 kernel (defconfig +
vroom additions) built by vcc that boots Debian in `qemu-system-x86_64
-enable-kvm`, with the same cold build time as on arm64.

**Why:** The machine is now an x86-64 Linux host. With x86-64 as a target the
tests run natively (no qemu-user), and vcc gets a second register-machine
target. It settles what in `arch/a64/codegen/` is really generic, what in the
Linux code is per OS and what is per CPU (TODO.md: "Target-OS code out of the
generic files").

**Rules:** the same as Phase 2 and PLAN-wasm.md. Clang is the reference
(`--target=x86_64-linux-gnu`), nothing from clang/LLVM at run time; tables are
taken from clang once (predefines, `__has_*`, capability tables). arm64 and
wasm output must not be changed by any step: every commit is verified with
`bench/vcc-same.py` against a base built before the step, and `bench/vbench.py
gate`.  x86_64-apple-darwin is not included.

## Starting point (2026-11-10)

- The host: x86_64, Linux 7.0, gcc and GNU binutils 2.46. **clang, lld and
  llvm-mc are missing**, and so is qemu. They are needed as a reference (see
  Risks: installation).
- **vcc does not build on x86-64**: the lexer is NEON (`frontend/lex/lex.c:4`, with a
  scalar path behind `VLEX_SCALAR`), interning uses ACLE CRC32C
  (`common/intern.c:1`, `include/common/intern.h:86`), `common/ldhash.c:184` has aarch64
  branches and the Makefile picks `-mcpu=...` (TODO.md: "x86-64 host build").
  `src/port/wasm/compat/` already shows how ACLE CRC and NEON are mapped when vcc is
  built for another target.
- The target is chosen by `po.target` (`include/frontend/pp/pp.h:82`: `PP_LINUX_AARCH64,
  PP_DARWIN, PP_WASM64, PP_WASM32`), with the macros `DARWIN(c)`/`WASM(c)` in
  `codegen/emitimpl.h` and `CC_ILP32(c)` in cc. The emitter picks the generator
  per function in `codegen/tu.c:178` (`wasm_emit_function` or `emit_function`),
  data directives in `codegen/data.c`.
- The arm64 generator (`arch/a64/codegen/`, ~10,300 lines) builds instruction
  records (`ins.h`) directly from the AST, with temporaries from a pool
  (`treg`/`tfree`), local variables in registers (`B_LREG`) or stack slots
  (`B_SLOT`), a post-optimizer (`opt.c`), a printer (`print.c`) and an encoder
  straight to the object model (`obj.c`). Folding, reachability, inlining and
  `__builtin_constant_p` remain there (W1 in PLAN-wasm.md did not lift them
  out).
- The object layer is arm64-bound: `include/obj/obj.h:54` includes `include/arch/a64/reloc.h`,
  and the ELF writer writes `e_machine = 183` (`platform/linux/elf.c:279`) and
  has AArch64 relocations in its GOT/TLS logic (`:42`).
- The linker (`ld/ld.c`) is one TU with arm64 fragments (`arch/a64/ld/*.inc`).

## The data model

| | aarch64-linux | x86_64-linux |
|---|---|---|
| `long`, pointer, `size_t` | 8 | 8 |
| `char` | unsigned | **signed** |
| `wchar_t` | `unsigned int` | **`int`** |
| `long double` | binary128, 16/16 | **x87 80 bits**, size 16, align 16 |
| `__float128` | - (= `long double`) | **binary128, its own type** |
| `va_list` | AAPCS64 struct | **`struct __va_list_tag[1]`** (`gp_offset`, `fp_offset`, `overflow_arg_area`, `reg_save_area`; 24 bytes, array type) |
| `_Float16`, `__bf16` | yes | yes (SSE2) |
| largest alignment | 16 | 16 |

LP64 like arm64, so the type table can stay. What is new is the x87 `long
double`: constant folding, literals and output (`.quad`+`.short`, 6 bytes of
padding) need a `lib/f80.h` next to `include/common/f128.h`, and `__float128` becomes its
own type.

## Architecture

| Part | File(s) | Contents |
|---|---|---|
| Host build | `frontend/lex/lex.c`, `common/intern.*`, `common/ldhash.c`, `src/Makefile` | SSE2/AVX2 lexer (scalar first), CRC32C via SSE4.2 (`_mm_crc32_u64`: same polynomial, same hashes), `pause` instead of `yield`, `-march=native` |
| Target selection | `include/frontend/pp/pp.h`, `driver/ppdriver.c`, `server/caps.c` | `PP_LINUX_X86_64`; `--target=x86_64-*-linux-gnu`, `-m64`; without `--target` on an x86-64 host: x86-64 (as the arm64 host gives aarch64) |
| Predefines, `__has_*` | `frontend/pp/ppdata-predef.inc`, `gen-ppdata.sh` | One more column from clang; the deltas of `-march=`/`-msse4.2`/`-mavx2`/`-mno-sse`/`-mgeneral-regs-only` as a table |
| Layout | `frontend/c/type.c`, `frontend/c/tu.c`, `frontend/c/layout.c` | `char` signed, `wchar_t` int, x87 `long double`, `__float128`, `va_list` |
| AST walker | `codegen/walk/` | Layer 1 below: folding, reachability, inlining, `__builtin_constant_p`, switch strategy, stack slots, `val`/`lval`, the call sequence; the lowering interface `walk/lower.h` |
| Records and passes | `codegen/rec/` | Layer 3: common record header, per-CPU op table, liveness, copy propagation, dead results, stack slots, jumps |
| Target table | `codegen/tu.c`, `codegen/data.c` | Function, header, tail and data directives per target; x86-64 is added next to the `WASM(c)` branches, which are tidied when arm64 and wasm are moved |
| Lowering | `arch/x64/codegen/` | Layer 2: `lower.c` (integer, memory), `fp.c` (SSE), `x87.c`, `abi.c` (SysV), `builtin.c`, `atomic.c`, `asm.c`; the op table in `x64impl.h`; `peep.c`, `twoaddr.c`, `func.c` (frame, prologue) |
| Text output | `arch/x64/codegen/print.c` | `-S` in clang's AT&T syntax (`.p2align 4, 0x90`, `.type`, `.size`, `#APP`) |
| Encoder | `arch/x64/as/enc.c`, `x64.h` | REX/ModRM/SIB/disp/imm, VEX/EVEX later; table-driven, the table generated once from LLVM's X86 tables |
| Assembler | `arch/x64/as/parse.c` | AT&T syntax for `vas`, inline asm and the kernel's `.S` |
| Fixups | `include/arch/x64/reloc.h`, `arch/x64/obj/fixup.c` | `R_X86_64_PC32/PLT32/32/32S/64/PC64/GOTPCRELX/REX_GOTPCRELX/TPOFF32/GOTTPOFF/TLSGD/TLSLD/DTPOFF32`; jump relaxation rel8<->rel32 as MC |
| Object writer | `platform/linux/elf.c` | `e_machine` and relocation rules from the arch instead of hard-coded |
| Linker | `arch/x64/ld/*` | Relocations, GOT/PLT, TLS and GOTPCRELX relaxation (no thunks) |

### The code generator's layers: AST walker, per-CPU lowering, common passes

An IR does not cost by existing but through three things: a graph with pointers
and use lists (allocation, cache misses), an extra lowering pass between IR and
machine, and SSA construction. vcc avoids all three but still writes each
optimization once. It does so through three layers:

```
frontend/c/'s AST ──▶ 1. AST walker (codegen/walk/) ──LO() calls──▶ 2. lowering (arch/x64/codegen/)
                                                                │ machine records, 3-address
                                                                ▼
                         3. common passes (codegen/rec/) ◀── per-CPU op table
                                                                │
                                         two-address, peephole, printer/encoder (arch/x64/codegen/)
```

**Common implementation plan (2026-11-11):** see
`PLAN-fas2.md`, **Shared optimizer without SSA**, OPT0-OPT8. C semantics,
strategies and record algorithms are owned there; X4 connects x86 through target
descriptions. General rules shall be used by x86, arm64 and wasm, not developed
as new x86 special cases. Arm64's existing optimizer is the source of the
shared record passes. Pure extractions preserve output byte for byte; when a
common rule is activated, its intended changes on all targets are allowed and
verified according to the common plan. This refines the older rule above about
unchanged arm64/wasm output.

**Current scope:** x86-64 is the first and only user. arm64 and wasm keep their
generators until they are moved, and that is a work item of its own (TODO.md:
"arm64 and wasm on the shared walker"). The interfaces are drawn for what
x86-64 needs. Two cheap preparations are made already now, because the move
would otherwise become more expensive: the calls go through `LO()`, and the
record header has arm64's layout. Nothing else is built in advance.

#### Layer 1: the AST walker, `codegen/walk/`

All decisions at C level are made here, once for all targets. The files follow
the topics of `arch/a64/codegen/`, and the rules are copied from there (folding,
`__builtin_constant_p`, inlining). That gives them a common form that arm64 can
move to later:

| File | Contents |
|---|---|
| `walk.h` | Internal header: `val`, `lval`, `bind`, `fnctx`, the walker's part of `ecx`; declarations of the `LO()` interface |
| `expr.c`, `cond.c` | Expressions, folding while the code is generated (`val` is a constant, address+offset or register), conditions to jumps, `cond_fold`/ranges, pointer arithmetic, `__int128` in register pairs |
| `lval.c`, `bind.c` | Lvalues (frame, symbol, register, bit-field), binding of locals to stack slots or registers (`B_LREG` from the prepass), stack slots and their reuse |
| `stmt.c`, `label.c` | Statements, the reachability flag, known values across edges (`kv_*`), cleanups, VLA, switch strategy (table, binary search, chain: the threshold is a target parameter) |
| `call.c`, `inline.c` | The call sequence around the target's ABI classification, inlining of `__always_inline` with constant arguments, `need_fold`, error() calls |
| `builtin.c`, `init.c` | Target-independent builtins (`constant_p`, `expect`, `object_size`, `choose_expr`, overflow, bit operations via `LO`), initializers, aggregate copies, soft floating point via `FN_*` calls (which types are soft is a target matter: x86 `__float128`, arm64 `long double`) |

Values are **physical registers** as today: the target hands out temporaries
(`LO(treg)`) and the walker frees them. No register allocation is needed.
Spilling happens as in arm64: around calls (`spill_live`) and a new attempt for
the function with `e->spill` when the temporaries run out. That logic lives in
the walker and applies to all targets. Loads are not merged with operations in
the walker (no `V_MEM` values). The peephole pass does that afterwards, and so
the walker is kept simple.

#### Layer 2: the lowering interface, `codegen/walk/lower.h`

Some thirty operations that `arch/x64/codegen/` defines. It is instruction
selection and nothing else:

| Group | Operations |
|---|---|
| Register | `treg`, `treg_fixed(R)` (moves away the one that holds R: `div`, shifts, `rep movs`, arguments), `tfree`, `mov` |
| Values | `iconst(rd, i128, width)`, `addr(rd, ref, off)` (rip-relative, GOT, TLS model: the target's business), `ext(rd, rs, from, to, sgn)` |
| Memory | `load(rd, lval, n, ext)`, `store(rs, lval, n)`, `lea(rd, lval)`, `copy(dst, src, n)`, `zero(dst, n)` |
| Integer | `bin(op, width, rd, val a, val b)` (b may be a constant), `un(op, ...)`, `bin2`/`cmp2` for register pairs, `cmp(op, sgn, width, a, b) -> cc`, `setcc`, `select` |
| Floating point | `fbin`, `fcmp -> cc` (parity for NaN is the target's business), `fconv` (integer<->float, between widths), x87 for `long double` is hidden here |
| Control | `label`, `jump`, `branch(cc, lbl)`, `branch_zero(val, nz, lbl)`, `jumptable(reg, lbls, n)` |
| Calls and frame | `abi_classify(fntyp) -> abiarg[]` (SysV eightbyte classification), `call(ref or reg, abiarg[])`, `ret`, `va_start`, `va_arg`, `prolog`/`epilog` (inserted when the function is done and the frame known) |
| Target-specific | `atomic(op, order, lval, ...)`, `builtin(id, args) -> bool` (the rest: `__builtin_ia32_*`, `cpu_supports`, `pause`), `inline_asm(node)` (constraints and modifiers are the target's; the walker computes the operands) |

The calls go through a macro, `LO(name)` -> `x64_name`. In the unity TU they are
inlined, so there are no indirect calls per AST node. *Preparation for the
move:* when arm64 becomes a second user, the walker is compiled once per target
(`#include "walk/*.c"` with a different prefix in `LO`, like a template). That
requires that the calls already go through `LO()`, and that is all that is
needed now.

#### Layer 3: machine records and common passes, `codegen/rec/`

The records are a flat array per function, as today, with a common header in
`codegen/rec/rec.h`:

```c
typedef struct ins {
	uint8_t op, sz;		/* op: the target's opcode; sz: width */
	uint8_t r[4];		/* register fields at fixed places (x86: rd, base, index, rs) */
	uint8_t cond, sh;	/* condition; scale/shift */
	union { int64_t imm; struct { int32_t off; uint32_t ref; }; uint32_t label; };
} ins;			/* 16 bytes, like arm64's */
```

The record is arm64's with the fields `rd, rn, rm, ra` as `r[0..3]`.
*Preparation for the move:* arm64 renames the fields and gets the same header,
and no layout changes.

Each target provides an **op table** (`opdesc[op]`): which `r[i]` are defined
and read, implicit registers (x86: `div` -> `rax rdx`, `rep movsb` -> `rdi rsi
rcx`, calls -> the caller-saved ones), jump kinds, `PURE`, reads/writes memory,
reads/writes `FLAGS`. Ops whose roles depend on the operands get a hook (like
`roles_sw` today). `rmask` is 64 bits: 16 GPRs, 16 XMMs, `FLAGS` and `FRAME`
fit.

Common passes, generic over the table and moved in form from
`arch/a64/codegen/opt.c`:

| Pass | File |
|---|---|
| Liveness over basic blocks (split at labels and after jumps) | `rec/live.c` |
| Copy propagation forward and backward, dead `mov` | `rec/copy.c` |
| Dead results (pure records whose output is not live; `FLAGS` counts) | `rec/dead.c` |
| Stack slots: sharing, loading from a register instead of the slot, dead spills (`SPILL_REC`) | `rec/slots.c` |
| Jump to the next record, jump to jump | `rec/branch.c` |

The records are **three-address** (`rd = a op b`) all the way up to a late x86
pass, `arch/x64/codegen/twoaddr.c`, which rewrites to two-address. There a `mov` is
inserted only when `rd != a` and `a` is live afterwards, and commutative
operations are swapped to avoid a `mov`. Copy propagation can therefore be the
same as arm64's, without rules for tied operands. It is LLVM's
TwoAddressInstructionPass in one linear pass.

Target-specific after the common passes: `arch/x64/codegen/peep.c` (load into the
operation when the temporary dies: `add 8(%rbp), %eax`; `lea` for add/shift;
`test r,r` against 0; `xor r,r` for 0; shortest `mov $imm`; `cmov`), then
`twoaddr.c`, frame and prologue (`func.c`), printer (`print.c`) and encoder
(`obj.c`). The encoding of selected records goes through the object layer's frag
and fixup replay (`IT_CODE`), which is already generic today.

The extension pass (`exts` in opt.c) is not moved now: x86's 32-bit operations
also zero the upper half, but that gives little at `-O1`. It is taken when
measurements show that it is needed.

#### What cannot be done without SSA

Global optimizations such as GVN, LICM and register allocation with graph
coloring. If `-O2` ever needs them, the way is an optional SSA pass on top of
the same flat records (virtual registers instead of physical ones), only for
`-O2` and only for functions with loops. It is not part of this plan.
`-O0`/`-O1`, which the kernel build uses, keep today's cost.

#### Two copies of the rules during the transition

Until arm64 is moved, the folding, inlining and `constant_p` rules exist both
in `arch/a64/codegen/` and in `codegen/walk/`. That they do not drift apart is
checked by `VCC_WALKLOG=1`. It writes, per function, what the generator decided: inlined
calls, folded conditions, remaining error() calls, the value of
`__builtin_constant_p` per call. A new harness, `bench/vcc-walkcmp.py`, compares
x86-64 with arm64 on the C suites (gcc-torture, tcc, vcc tests). Differences are
accepted only where the data model explains them (`char`, `wchar_t`, `long
double`), and they are listed in the harness's exceptions file.

### x86-64 in the lowering

What distinguishes x86-64 and what layer 2 and the x86-specific passes take care
of:

- **Registers:** 16 GPRs. Temporaries from the caller-saved ones (`rax rcx rdx
  rsi rdi r8-r11`), local variables in the registers `rbx r12-r15` (`B_LREG`).
  Operations with fixed registers (`div`/`idiv` (`rdx:rax`), shifts (`cl`),
  widening `mul`, `rep movsb`/`stosb` (`rdi rsi rcx`), the argument registers)
  take their register through a `treg_fixed(e, R)` that moves away the one that
  holds it. That is the big difference from arm64's plentiful registers; spill
  to stack slots when the pool runs out.
- **Two-address and memory operands:** the records are three-address until
  `twoaddr.c` writes `rd = a op b` as `mov a, rd; op b, rd`. `peep.c` merges
  load+operation (`add 8(%rbp), %eax`), uses `lea` for add/shift combinations,
  `test` for comparison with 0, `xor r,r` for 0 and `cmov`/`setcc` for
  conditions.
- **32-bit operations zero the upper half**, like arm64's w registers: the same
  extension rules can be reused.
- **Floating point:** `float`/`double` in scalar SSE2 (`addsd`, `cvtsi2sd`,
  `ucomisd`). `==` and `!=` need the parity flag (NaN). Unsigned 64-bit
  conversions with clang's sequences. `long double` in x87 via memory slots
  (`fldt`/`fstpt`), without register allocation for the x87 stack; `__float128`
  via compiler-rt (`__addtf3` ...). `-mno-sse`/`-mgeneral-regs-only`: no SSE
  instructions (`bench/vcc-fpcheck.py`).
- **ABI (System V):** classification per eightbyte (INTEGER/SSE/SSEUP/X87/
  X87UP/MEMORY), aggregates <= 16 bytes in up to two registers (also mixed),
  larger ones via memory; sret in `rdi`, returned in `rax`; `long double` in
  memory as an argument and in `st(0)` as a return value; `__int128` in register
  pairs. Varargs: `al` = number of vector registers, `va_start` fills the
  register save area, `va_arg` as clang's. The stack is 16-aligned at calls. Red
  zone (128 bytes) for leaf functions unless `-mno-red-zone`.
- **Addressing:** `x(%rip)` as the default (small code model); `-fPIC`/`-fPIE`:
  `x@GOTPCREL(%rip)` and `call x@PLT` as clang. TLS: LE `%fs:x@tpoff`, IE
  `x@gottpoff(%rip)`, GD/LD with clang's exact padding (`data16 leaq ...;
  data16 data16 rex64 call __tls_get_addr@PLT`) so that the linker can relax.
- **Jump tables:** `switch` with dense cases as clang's (`.rodata`, 32-bit
  relative entries in PIC, absolute 64-bit otherwise).
- **Atomics:** TSO makes loads and stores plain `mov`; seq_cst store as `xchg`;
  RMW with a `lock` prefix or a `cmpxchg` loop; 16 bytes via `cmpxchg16b` under
  `-mcx16`, otherwise `__atomic_*` calls as clang.
- **Builtins:** `clz`/`ctz` (`bsr`/`bsf`, or `lzcnt`/`tzcnt` with
  `-mlzcnt`/`-mbmi`), `popcount` (`popcnt` under `-mpopcnt`, otherwise clang's
  bit sequence), `bswap`, overflow arithmetic via `seto`/`setc`,
  `__builtin_cpu_supports`/`__builtin_cpu_init` (`__cpu_model` in compiler-rt),
  `__builtin_ia32_pause`/`rdtsc`/... which the kernel uses.
- **Inline asm:** AT&T text with `%k0 %w0 %b0 %h0 %q0 %P0 %c0 %a0`, constraints
  `r q Q a b c d S D A m i n e Z x t u`, flag outputs `=@ccz` (the kernel uses
  them), `asm goto` with outputs.

## Steps

| # | Deliverable | Criterion |
|---|----------|-----------|
| X0 | **vcc builds and runs on x86-64 Linux** (still aarch64 as the target). Scalar lexer via `VLEX_SCALAR`, CRC32C with SSE4.2, host `#ifdef`s in `ldhash.c`, Makefile per host CPU. SSE/AVX2 lexer afterwards as a performance step | `make -C src vcc` and `fuzz-regress` green; `vcc-same.py` against output made on the Mac (arm64 output from an x86 host byte-identical); parity `vcc-diff.py cmp`; closes TODO "x86-64 host build" |
| X1 | **Target selection and front end.** `PP_LINUX_X86_64`, predefines from clang, layout (the table above), `lib/f80.h` for x87 constants, `__float128` | `vpp -dM -E` = `clang -dM -E` (also `-march` deltas, `bench/vcc-predef.py`); `vcc-sema.py layout` with x86-64 as the target; `vcc-ldconst.py` for x87; `vcc-bflayout.py` for x86-64; arm64/wasm byte-identical |
| X2 | **The layers in place, integer `-S`.** `codegen/rec/rec.h` and x86's op table; `codegen/walk/` with `lower.h` and `LO()`; x86-64 in the target table (`codegen/tu.c` picks the walker for x86-64); `arch/x64/codegen/` for integers, locals in stack slots, parameters, direct calls, `if`/loops/`break`/`continue`/`switch` (chain), `return`; prologue/epilogue with `rbp`; `twoaddr.c` (needed at all levels: the records are three-address), `print.c`. Only `-O0`, no optimization passes | `-S` is assembled by `clang -c`, linked against glibc and run natively with the same result as clang's; new `bench/vcc-x86.py` (like `vcc-wasm.py`: the test headers against glibc, native run, cache); arm64/wasm byte-identical (`vcc-same.py`) |
| X3 | **All of C in the walker and the lowering.** Aggregates and initializers, SysV classification (`abi.c`), varargs, jump tables, floating point (SSE, x87), `__int128`, `__float128` via `FN_*`, bit-fields, VLA/`alloca`, atomics, TLS, PIC/PIE, builtins, inlining and `need_fold`, known values, spill; `VCC_WALKLOG` | gcc-torture and tcc-tests2 at `-O0`: the same passes as clang; `vcc-abi.py --target x86_64` (clang/vcc x caller/callee, gcc as a third party); `vcc-pic.py`, `vcc-fpcheck.py` for x86-64; `bench/vcc-walkcmp.py` against arm64 green |
| X4 | **Hooking up the common optimizer and `-O1`.** `PLAN-fas2.md` OPT0-OPT8 own the rules/algorithms; x86 fills in the op/effect tables and connects `codegen/rec/`, local registers and target operations. SIB/LEA, SSE2, target features, prologue and two-address remain x86. No separate copy of generic optimization policy. | C regressions on all three backends, x86 code requirements and ABI at O0-O3; pure extraction byte-identical on arm64. Measure the compiler's cost and the generated code according to the OPT plan; confirm x86 support before `vbench.py` is named as the x86 gate. |
| X5 | **Object files.** Encoder for what the generator writes, fixups, ELF machine 62, jump relaxation; `-c` directly from the records without text; CFI and DWARF with x86 register numbers | `vas-obj.py`: vcc's `-c` byte-identical to `clang -c` of the same `.s`; `vas-enc.py` against llvm-mc; X4's suites with `-c`; `vcc-cfi.py` and `vcc-dwarf.py` for x86-64 |
| X6 | **Inline asm and `vas` for x86-64.** AT&T parser, constraints and modifiers, flag outputs, `asm goto`; the whole ISA including SSE/AVX/AVX-512 (the kernel's crypto `.S`) | `vas-corpus.py` for x86-64 (`clang -S` of the test files, `clang -c` against `vas`); the kernel's `.S` byte-identical to clang's objects |
| X7 | **vlink for x86-64.** Relocations, GOT/PLT, TLS relaxation (GD/IE->LE), GOTPCRELX relaxation | `bench/vcc-x86.py` with vlink: loaded image byte-identical to lld's; `vlink-opts.py` |
| X8 | **The kernel.** `-mcmodel=kernel`, `-mno-red-zone`, `-mno-sse -mno-mmx -mno-80387`, `-mstack-alignment=8`, `-mfentry -mrecord-mcount`, retpoline/rethunk (`-mretpoline-external-thunk`, `-mfunction-return=thunk-extern`, `-mindirect-branch-cs-prefix`), IBT (`-fcf-protection=branch`, `endbr64`), `-mharden-sls=all`, `-fpatchable-function-entry`, stack protector via `%gs` (`-mstack-protector-guard-*`) | objtool without warnings on all objects; `vcc-diff.py`/`vcc-sema.py` on the x86-64 tree; boots Debian in `qemu-system-x86_64 -enable-kvm`; vccd build with timings |
| X9 | **Intrinsics.** `__builtin_ia32_*` with vector types for clang's `immintrin.h` (SSE4.2 CRC, `popcnt`, AVX2) | `vcc-builtins.py` for x86-64; PostgreSQL built and tested on x86-64 Linux (`bench/pg/`) |

The order makes each step visible: X2 gives runnable programs via clang's
assembler and all three layers in minimal form, X4 optimized code, X5 our own
objects, X8 the kernel. X0 is the precondition for everything (nothing can be
run here otherwise) and X1 can be started in parallel with X0's lexer part. X9
is not needed for the kernel and can wait.

## Validation

- **Reference:** clang with `--target=x86_64-linux-gnu`. `-S` is not compared
  line by line (instruction selection differs); what is compared is behavior
  (native run), ABI (calls between clang, gcc and vcc objects), layout
  (`vcc-sema.py layout`) and objects for the same `.s` (byte for byte, like
  `vas-obj.py`).
- **Running:** natively on the host, no emulation. The same cache as
  `vcc-ctests.py`: an object byte-identical to one that has passed is not run
  again.
- **arm64 and wasm unchanged:** `vcc-same.py` with a base from `vbench.py
  mkbase` in every commit (the x86-64 code sits alongside, but `codegen/tu.c`,
  `codegen/data.c`, `frontend/c/` and `obj/` are shared); `vbench.py gate`.
  The arm64 runs (ctests with qemu-aarch64, `vcc-abi.py`) are done on the Mac or
  in the `env/linux/` container until qemu-user is available here.
- **Performance:** x86-64 output should cost about the same as arm64 per
  function. Measured with `vbench.py` on the x86-64 kernel's TUs when X5 is done;
  the encoder is what can cost (variable length, relaxation).

## Risks and open questions

- **Clang version:** the host has clang/lld/llvm-mc 22 (`/usr/lib/llvm-22/bin`),
  qemu-system 10.2 with KVM and qemu-user with an aarch64 sysroot (2026-11-10).
  The arm64 tables come from clang 23. x86-64's predefines and `__has_*` (X1)
  should be taken from the same version, otherwise version differences show up
  as target differences. clang 22 is enough for X0 and for the behavior tests.
- **Register shortage and fixed registers:** 16 GPRs, some of them tied by
  `div`, shifts and arguments. The walker's spill (around calls, and a new
  attempt with `e->spill`) must suffice without the x86 lowering spilling on its
  own. Tested in X3 with gcc-torture's deep expressions. If it is not enough, the
  next step is spill per operand in the walker, not in `arch/x64/`.
- **Byte-identical objects require MC's relaxation exactly:** the same fixpoint
  order for `jmp`/`jcc` rel8/rel32, the same multi-byte `nop` for `.p2align`, the
  same choice between equivalent encodings (e.g. `mov` with `89` or `8b`). The
  encoder table is taken from LLVM once, so the choices come along.
- **The rules drift apart** between `codegen/walk/` and `arch/a64/codegen/` during the
  transition: `vcc-walkcmp.py` in X3 and X4. A rule change is made in both until
  arm64 is moved.
- **The interface does not fit arm64 later:** it is drawn only for x86-64. The
  risk is kept down by the record header being arm64's and the layers following
  arm64's file layout. What still does not fit is changed when arm64 is moved.
- **The kernel:** `__seg_gs` (percpu with named address spaces) if
  `CONFIG_USE_X86_SEG_SUPPORT` is turned on for clang; objtool requires that
  vcc's control flow can be followed (no jump tables objtool does not recognize,
  the right `.discard.*`/annotation sections, `ud2` after `noreturn` calls as
  clang).
- **x87:** folding of 80-bit values must round exactly as clang's APFloat
  (precision 64 bits, denormals); `vcc-ldconst.py` with x86-64 as the target
  decides.
- **OS versus CPU:** what turns out to be common to arm64 and x86-64 Linux
  (TLS model choice, GOT rules, section names) is moved to `platform/linux/`;
  that closes the TODO item about the Darwin branches.

## Status

### X5 (2026-11-11): direct encoding from the records, plan

Measured: kernel/fork.c `-c` 0.15 s against `-S` 0.11 s; the assembler on the
text is ~25 % of the TU. Path, step by step, each step byte-identical
(`vcc-x86.py --objcmp`):
1. An assembler session without text (`as_open/as_close` on ElfObj, as `as_run`
   does but without a source): sections, symbols, labels (`as_label`), jumps via
   `as_branch` (the same relaxation), and `as_line` for what may be text
   (data, directives, CFI, `.loc`, inline asm) in the same session.
2. `x64_encode(e, rec)`: the records X_MOV/X_LD/X_ST/X_LEA/ALU/X_CMP/X_TEST/
   X_SETCC/X_CALL/X_JMP/X_JCC/X_PUSH/X_POP directly to `XForm` + `x64_emit`
   (the assembler's encoder: MC's choices are inherited); other records as a
   line of text.
3. The share of text lines towards zero: the SSE/x87 records, prologue/epilogue,
   CFI (`cfi_*` calls instead of `.cfi_*` text), `.loc` (dw_row directly).
4. Data and section headers directly (like arm64's IT_SYMHDR/IT_DATA).
Validation per step: --objcmp 100 %, -g/-fasynchronous-unwind-tables, the
kernel's TUs (`vcc -c` against `vcc -S | vasm`: the same object), time per TU
(vbench).

### X8 (2026-11-11): the x86 kernel built with vpp boots Debian

- `work/vcc-x86-cc.sh` (vpp as CC, only the version queries to clang),
  `make O=work/build-x86 ARCH=x86_64 LLVM=1 CC=... defconfig vmlinux`.
  Kbuild's probes now give the same `.config` as clang's (except CC_CAN_LINK):
  unknown -W and gcc flags are rejected as clang does, -D that redefines a
  predefined macro warns.
- The hardening the kernel asks for, written as clang writes it:
  `-fcf-protection=branch` (endbr64, the IBT note),
  `-fpatchable-function-entry=N,M` (nops, the section
  `__patchable_function_entries` with SHF_LINK_ORDER per function),
  `-mfunction-return=thunk-extern`, `-mretpoline-external-thunk` with
  `-mindirect-branch-cs-prefix`, `-fstack-protector*` with
  `-mstack-protector-guard-reg/-symbol`, `-ftrivial-auto-var-init=zero`.
- Fixed along the way: static branches (address arguments bound at inlining, "i"
  from the front end's constant), ASM_CALL_CONSTRAINT (%rsp), cpuid (output via
  pointers), call_on_stack (callee-saved operands), rip_rel_ptr,
  `#pragma GCC visibility`, dead && operands, asm goto with outputs,
  ALTERNATIVE (.skip/.fill as the layout decides), the x86 assembler (SSE4, AES,
  SHA, CRC32, AVX/AVX2 via VEX, system and CET instructions, control
  registers, register aliases), the x86 assembler split per instruction family.
- Boots Debian 13 (2026-11-11): `bench/boot-test.py --arch x86_64` BOOT OK,
  running, 0 broken units, done in 4.9 s (KVM; 44 s with TCG). Linked with
  vroom-ld (5 stubs), 25 objects still via clang (`work/vcc-x86-fallback.txt`).
  Found by bisecting per object from a whole clang build that boots, and a gdb
  watchpoint on `__preempt_count`: asm flag outputs (`=@ccc`, boot_cpu_has),
  scoped_guard (the cleanup ran twice), and the frames (the 16 KiB stack was not
  enough: temporary slots and the locals of inlined copies are now reused, the
  chain 20.6 KB -> 13.3 KB).
- Remaining: the 25 clang objects (`.code32`/`.code16`: realmode, efi-mixed,
  vdso32, compat entry; AVX-512 (EVEX) in crypto; `.org` targets), linking with
  vlink instead of vroom-ld, objtool.

### X4 (2026-11-11): started, folds in the walker

- `wk_cond` and `if`/`?:` use arm64's shape folds (`cond_fold`: x >> 0 != x,
  x < y && x > y, ranges); a condition that always jumps makes the branch dead
  (`wkst.cjump`), and `?:` with a known condition (after inlining) generates one
  arm.
- 0 << x, 0 >> x, -1 >> x are folded; loads of const globals (integers, and
  (int) of a const double) are folded like arm64's `const_global`.
- a[K] = constant for local arrays that are only used as a[CONST] (no SA_AESC)
  carries over to loads in straight-line code (`wkst.ael`; a label forgets
  them), like arm64's ael: pure-1.
- (float)floor((double)f) -> floorf(f) (also ceil, trunc, round, rint,
  nearbyint, fabs) with optimization, like clang -O2: 20030125-1.
- `vcc-x86.py -j 6`: 1776/1776; --pic, --obj and -g the same; --objcmp
  1846/1846, --fpcheck 1908/1908, --vlink 1758/1758, vcc-abi x86_64 12/12; arm64
  unchanged.
- Remaining in X4: a real pass on records (register allocation, copy
  propagation, like arm64's opt.c) and -O1 as a mode of its own.

### X7 (2026-11-11): vlink for x86-64, static links done

- `arch/x64/ld/x64.inc` (included by `ld/ld.c`): relocation scan and
  application, GOTPCRELX relaxation, GD/IE->LE, TLS variant 2
  (`tls_tp` = end of the block), 4K page size as the default, `SHT_X86_64_UNWIND`
  as input (output PROGBITS as lld), int3 padding in executable sections,
  empty `.got` kept after relaxed GD/LD, `__tls_get_addr` not undefined.
- `.note.gnu.property`: when the AND of FEATURE_1_AND is 0 all are dropped (as
  lld); otherwise the input is kept (merged note: remaining).
- Test runtime: `test/rt/start-x86.S`, `link-x86.ld` (static, own TLS).
- `bench/vcc-x86.py -j 6 --vlink`: 1750/1751 loaded images byte-identical
  with lld's and run; remaining 20030125-1 (floor->floorf, requires the
  optimizer). arm64 unchanged: `VCC_LDCMP=1 vcc-ctests.py` 1780/1780, vlink-opts,
  vlink-bad.
- Static PIE (`--vlink --pie`: rt with -fPIE, `-static -pie --no-dynamic-linker`,
  start-x86.S applies R_X86_64_RELATIVE): 1749/1750 byte-identical and run.
  An empty `.rela.dyn` that the script names becomes RELA as lld's (only x86 so
  far).
- Remaining: shared objects and dynamically linked programs (PLT, .dynamic) for
  x86.

### X5/X6 (2026-11-11): assembler and objects, started
- `arch/x64/as/asm.c`: AT&T parser and encoder (integer, SSE, x87, prefixes,
  `@PLT`/`@GOTPCREL`/TLS modifiers), with MC's choice of form (accumulator
  forms, imm8/disp8, `c7`/`b8`, `d1` for shift by 1); jumps to labels as relaxed
  records in `as.c` (`VI_X86BR`, short until the distance or the target requires
  the near form with `PC32`/`PLT32`). `arch/x64/obj/fixup.c`: x86's fixups
  (resolved in place as MC, GOT/TLS keep the symbol); `ElfObj.machine`
  (EM_X86_64: no mapping symbols, MC's nop padding); `#` comments, quoted
  labels. `vasm --target=x86_64-linux-gnu`; `-c` for x86-64 goes through the
  text (`E.objtext`) and the assembler.
- `vcc-x86.py --objcmp`: vcc's `.s` through vasm and through `clang -c`, the
  objects byte for byte: 1846/1846. `vcc-x86.py --obj` (vcc's side with `vpp
  -c`): 1768/1776, as with `-S`; the same with `--pic`. arm64: `vas-obj.py`
  45/45.
- `vas-corpus.py --target x86_64-linux-gnu` (clang's `-S` of the test files at
  `-O0`/`-O2`, vasm against `clang -c`): 3679/3680 identical (SSE2 integer, x87
  forms, `shld`/`bt`/`prefetch`, CFI with x86's CIE and `.eh_frame` as
  `SHT_X86_64_UNWIND`, MC's relaxation against the previous pass's layout).
  Remaining: `20050224-1 -O2`, a relaxation case at a `.prefalign`.
- CFI (`-fasynchronous-unwind-tables`: prologue and epilogue as clang, saved
  callee-saved) and `-g` (line table from the walker, `.loc` before the records,
  `twoaddr` moves the lines) in the x86 printer; `.word` is 2 bytes on x86; the
  DWARF directory for a shared `.file` path as MC; `.quad x@DTPOFF` etc. in
  data. With `VCC_CFLAGS='-g -fasynchronous-unwind-tables'`: the suite 1767/1775
  with `-S` and `--obj`, `--objcmp` 1846/1846; `backtrace()` and gdb go
  through vcc's functions; `vas-corpus` with `-g` 3679/3680.
- Remaining: encoding directly from the records (without text), the whole ISA
  (AVX, AVX-512 for the kernel's crypto) and `vas-enc.py` for x86-64.
### X3 (2026-11-11): interrupt calling convention

- `interrupt` gets the hardware frame/error parameter from the stack, saves all
  GPRs, clears DF during the handler and returns with IRETQ. `-muintr` gives
  UIRET and `__UINTR__`. Wrong signatures and direct C calls are diagnosed.
- Without `-mgeneral-regs-only`, x87/MXCSR/XMM are saved with
  FXSAVE64/FXRSTOR64; a private empty x87 stack makes even a full interrupted
  stack safe. YMM/ZMM upper halves are not used by vcc; external code/inline asm
  that changes them must preserve them itself. Kernel code without enabled FP
  state uses `-mgeneral-regs-only` as usual.
- `bench/vcc-interrupt.py`: actual synthetic IRETQ returns, with/without error
  code, all GPRs/XMMs, full x87 stack, DF and over-aligned locals;
  `-O0/-O1/-O2`, both `-S` and `-c`. The UIRET encoding and incoming frame are
  verified, but the instruction itself requires OS/hardware support and is not
  run.

### X4 (2026-11-11): implementation plan moved to the common emitter

Code review after the rebase on main: arm64 already has register locals and
record passes, x86 lacks the hookup, wasm has its own locals and structured
lowering. `PLAN-fas2.md` **Shared optimizer without SSA** has OPT0-OPT8 with
concrete files, dependencies, cost limits and tests on all three backends. The
x86 rules of the earlier audits are findings/prioritization, not a decision to
implement them in x86's `lower.c`. No optimization implemented; X4 open.

### Register locals, partial progress 2026-11-11

OPT3 in `PLAN-fas2.md` has common selection and x86's four callee-save homes,
with asm/nonlocal/alignment safety. Own suite 137/137, cross-ABI 22/22;
all cold audit probes unchanged after the profitability filter. A small loop
improves but the compiler-CPU gate is still open. Direct ABI binding is now
delivered and verified with narrow arguments,
stack arguments, sret and varargs. Copy cleanup remains; full O2 parity
is not reached. See OPT3's next status block for paired measurements.

### X4 (2026-11-11): reproducible level matrix and test probes

`bench/vcc-optaudit.py` compares 29 functions in
`bench/optaudit/probe.c` at O0/O1/O2/O3, baseline x86-64 and Ryzen-native.
Run: `PATH=/usr/lib/llvm-23/bin:$PATH python3 bench/vcc-optaudit.py --native`.
Separate worktree `audit/x86-optimizations`, vcc built from `52261ef`,
clang 23.1.6 and GCC 15.2.0 on a Ryzen 7 9700X. The main worktree's
uncommitted changes are not included. No compiler code changed.

All 24 combinations compile, assemble, link and give the same
checksum `1922a5c83358247b`. The same clang-built caller is used for all
objects. It tests 512 deterministic input sets, loop lengths
0-64, signed division with positive/negative values and explicit
boundary values including LONG_MIN/ULONG_MAX. This is a targeted differential
check, not a proof for all inputs. No timing or heavy load.
Distro hardening is normalized with `-fno-stack-protector -fcf-protection=none`;
all use `-fno-pie`.

**The matrix below gives the first observed level for the probe in question.**
`missing` means that the transformation does not happen at any of O0-O3 in this
probe, not that a whole optimization area has been exhaustively tested. A higher
level may choose other code, and the same effect may be achieved by several
passes.

| Transformation / probe | vcc | Clang | GCC |
| --- | --- | --- | --- |
| Literal constant folding (`fold`) | O0 | O0 | O0 |
| Constant local scalar replaced by literal (`local_const`) | O1 | O1 | O1 |
| Known array elements folded (`array_const`) | O1, but stores remain | O1, array gone | O1, array gone |
| `always_inline` (`always`) | O0 | O0 | O0 |
| Small explicit `inline` (`inline`) | O1 | O1 | O1 |
| Small ordinary static function inlined (`auto_inline`) | missing | O1 | O1 |
| Remove `+0` and `*1` entirely (`identity`) | missing | O1 | O0 |
| Unsigned `/8` with shift (`div8`) | missing | O0 | O0 |
| Unsigned `%8` with mask (`mod8`) | missing | O0 | O0 |
| Signed `/8` without idiv (`sdiv8`) | missing | O1 | O0 |
| Unsigned `/10` without div (`div10`) | missing | O1 | O0 |
| `x*9` with one LEA (`mul9`) | missing | O1 | O1; shift/add already O0 |
| `p[i+3]` as one indexed memory operand (`indexed`) | missing | O0 | O1 |
| Scalar parameters/locals without stack spill (`add32`, `aggregate`) | missing | O1 | O1 |
| Store-immediate (`stores`) | missing | O0 | O0 |
| Remove overwritten store (`stores`) | missing | O1 | O1 |
| Reuse non-volatile load (`reload`) | missing | O1 | O1 |
| Keep both volatile reads (`volatile`, control case) | O0-O3 | O0-O3 | O0-O3 |
| Common subexpression computed once (`cse`) | missing | O1 | O1 |
| Eliminate the second range test (`range`) | missing | O1 | O1 |
| Wrapper as tail jump (`tail`) | missing | O1 | O2 |
| Dense constant switch results as a value table (`switch`) | missing | O1 | O2; code jump table O1 |
| Explicit 128-bit integer vector with packed add (`vector_add`) | missing, scalar | O0 | O0 |
| Sum loop automatically vectorized (`sum`, baseline) | missing | O2 | O3 |
| Sum loop automatically vectorized (`sum`, native) | missing | O2 | O2 |
| Invariant `x*y+1` out of the loop (`licm`) | missing | O1 | O1 |
| Restrict copy loop becomes memcpy (`copy`) | missing | O1 | O2 |
| Unused local computation removed (`dead_local`) | missing | O1 | O1 |
| Constant if: dead arm removed (`cond_fold`) | missing; the branch is chosen but dead instructions remain | O0 | O0 |
| POPCNT with a native target (`popcount`) | missing, SWAR | O0 | O0 |

**vcc's levels:** `driver/ppdriver.c` passes opt=0 for O0, opt=1 for O1
and opt=2 for O2/O3. All opt checks in the x86 walker only test
zero/nonzero; `x64_finish` has only the two-address pass. O1/O2/O3 give
byte-identical assembler in this corpus, both baseline and native.
Native does not change vcc's assembler here. This shows no effective
native instruction selection for these cases; it does not mean that the host
should always be the target machine. FP temporaries already live in SSE
registers; TODO's older wording about FP registers does not apply to x86
as it stands.

**Enabled options versus actual transformations:**
`build/linux/optaudit-levels/` contains per combination `.s`, `.o`,
`.dis`, `.passes` and the executable; for GCC also `.options` and `.remarks`.
`report.json` has commands, versions, CPU, revision, SHA256 of vpp and the
probes, function sizes and raw instructions and pass inventories.
GCC's options come from `-Q --help=optimizers`, the pass list from
`-fdump-passes`; Clang's observed passes and remarks from
`-Xclang -fdebug-pass-manager -Rpass=.* -Rpass-missed=.* -Rpass-analysis=.*`.
GCC's flag list, its configured passes and LLVM's executed passes are different
measures. ON does not mean that a pass changed the program; the LLVM list is
not a complete inventory of all backend transformations.

Example: LLVM runs `LoopVectorizePass` even at O1 here but the sum loop
is vectorized only at O2. GCC has loop vectorization enabled at O2
but chooses scalar baseline code in the sum probe; native gets vector code
already at O2. O3 thus does not make a transformation automatically suitable.

Observed difference O2->O3: GCC adds, among others, loop interchange,
unroll-and-jam, peeling, unswitching, predictive commoning and IPA
constant-propagation cloning. LLVM's log adds the pass names
`ArgumentPromotionPass`, `CallSiteSplittingPass` and
`ControlHeightReductionPass`; other passes that were already present may change
budget and heuristics without a new name appearing. These areas
are inventoried but have no isolated behavior probes of their own in this round.
vcc has no corresponding general analysis/pipeline on x86 in the audit base.

**From probes to regression tests:** the run check is already a test base.
The harness also requires two volatile input reads, folding of the
literal expression and absence of calls in `always_inline` at all levels.
For each implemented optimization a targeted code requirement is added, e.g.
no div in unsigned `/8`, packed add for an explicit vector, or a single
non-volatile read. The requirements shall be function- and target-scoped,
not a whole assembler snapshot or a general instruction-count ceiling.
Missing optimizations are now reported as findings, not as passed tests.
Future memory optimizations need negative cases for alias, volatile,
atomics and asm; loop optimizations need zero iterations; inlining
needs side effects and ABI cases. Performance is measured separately for both
the compiler and the generated code. This developer audit is not a new CI gate:
it requires native x86, two reference compilers and GNU binutils and
also compares missing properties. G5's benchmark requirement is still open.

GCC uses assembly and optimization dump checks
([dg-final](https://gcc.gnu.org/onlinedocs/gccint/Final-Actions.html)); LLVM
has isolated pass/IR and backend tests with FileCheck
([TestingGuide](https://llvm.org/doc/TestingGuide.html)) and a separate
helper-program suite for correctness, run and compile time
([TestSuiteGuide](https://llvm.org/doc/TestSuiteGuide.html)). Their tests
are good models for structure but no guarantee of an optimal result
on arbitrary code. For vcc, small C tests and machine-record/assembler
requirements are more directly reusable than LLVM's IR tests.
Our existing `vcc-gcc-dg.py` rejects contracts with `dg-final` (tested in
`test-gcc-dg.py`); so it does not run GCC's assembly checks today.
Reusing such upstream cases requires a deliberate extension or
port of the code requirement, not just that the files compile and run correctly.

### X4 (2026-11-11): audit of cheap optimizations without SSA

Audit in a separate worktree `audit/x86-optimizations`, base `52261ef`. Here
cheap means low compiler cost: local lowering and at most one linear
pass over the machine records, no fixpoint iterations or global alias analysis.
No optimization implemented; X4 is still not done.

On a Ryzen 7 9700X, 13 small functions were compared with vcc `-O2`, clang
23.1.6 and GCC 15.2.0 `-O2 -march=x86-64`; the references also with
`-march=native`. This is assembler inspection, not measured run time or
speedup. The base is committed code, not the main worktree's ongoing changes.
The probes and the five assembler files are local in `build/optaudit/` in the
audit worktree.

| Order | Gap and evidence | Cheap implementation without a new IR/SSA |
| --- | --- | --- |
| 1 | `unsigned long x / 8` and `x % 8` use `divq`; both references use `shr` and `and` respectively | Special case for a constant power of two in `x64_bin`, O(1) per operation. Signed division needs a bias for rounding toward zero; start with unsigned. Keep the error/UB behavior for zero separate. |
| 2 | `add32` contains `movl %r10d,%r10d`; `indexed` sign-extends an already sign-extended result; returns jump to the next label | A forward pass with a small table of the registers' known width/extension, and removal of jumps to an immediately following label. Reset the facts at labels, calls and asm. A 32-bit self-move can zero the upper half and may only be removed when it is already known to be zero. |
| 3 | `x*9` uses `imulq`; the references `leaq (rdi,rdi,8)` | O(1) rules for powers of two and 3/5/9; add index/scale to the LEA lowering. Keep the flags semantics or prove that they are not used. Avoid long shift/add chains for arbitrary constants. |
| 4 | `p[i+3]` uses a separate add/mul/add; the references `movl 12(rdi,rsi,4),eax` | Match the address's AST at the memory operation; base + index*1/2/4/8 + displacement. Requires an extension of `lval`/records/printer, but no analysis over the whole function. Respect width, signed index and evaluation order. |
| 5 | `*p=2` materializes 2 in a register | Store-immediate directly at lowering. x86's 64-bit store-immediate takes only a sign-extended imm32. Additionally removing an earlier store is a separate memory-analysis question. |
| 6 | Native references use `popcntq`; vcc uses SWAR | Choose the instruction with an already computed target-feature bit. Requires correct `-march`/`-m*` handling, not checking of build hosts in the backend. SWAR is a valid baseline and is not in itself an optimization bug. |
| 7 | Explicit `vector_size(16)` unsigned add goes lane by lane via the stack; baseline references use `paddd` | Direct SSE2 lowering for a small set of explicitly specified vector operations, O(1) per operation. No automatic vectorization or loop analysis needed. |

The next level without SSA, but larger work:

- **Constant division beyond powers of two.** `/10` uses `divq`, against the
  references' multiply-high + shift. Compute the magic multiplier with a
  verified algorithm, at most O(bit width) per constant, preferably cached per
  TU. Test 32/64 bits, signed/unsigned, extreme values and remainder
  separately.
- **Local register cache for frame slots.** Parameters and locals live
  in memory; all incoming parameters get slots in `wk_function`.
  A limited slot->register cache within a basic block can reduce
  traffic without SSA. Invalidate for aliasing stores, address escape, asm,
  calls, joins and volatile/atomic access; physical register pressure must be
  handled. This is larger than a peephole, but likely of broader benefit.
- **Switch.** Eight constant return cases become a comparison chain; both
  references use a value table. Value table for dense constant cases,
  jump table for dense code cases, balanced tree for sparse cases. Limit the
  table size; sorting O(k log k), not quadratic search.
- **Tail calls.** A simple wrapper makes an ordinary call and epilogue, against
  a `jmp` in the references. Limit first to ABI-compatible direct
  calls without stack arguments, cleanup, dynamic stack or the address of its
  own frame that the callee could use.
- **Local CSE/load forwarding/DSE.** `reload` reads the same pointer twice;
  `stores` keeps both writes. Start only with exact,
  non-address-escaped frame slots. The current `rec` lacks explicit
  volatile/atomic marking; deleting arbitrary memory records
  is therefore not a safe first rule. Put effects and implicit
  registers/flags in the op table before general record passes.

Existing and not to be counted as missing: constant folding,
condition folding with preserved side effects, some local constant propagation,
small `inline`/`always_inline`, immediates and direct comparison branches.
`x64_finish` today runs only `x64_twoaddr` before printing; the register is
chosen physically at AST lowering. None of the proposals requires changing that
setup.

Priority: 1-5 first, then the local register cache; target features and explicit
SIMD as separate steps. LICM, automatic vectorization, global GVN and
aggressive inlining do not belong in this cheap first round. SSA is not
a theoretical requirement for them either, but they need more analysis and
budget. Measure each implemented step with instructions for the compiler and
run time/code size for the result; no performance numbers are assumed by this
audit.

References for terminology and comparison:
[LLVM passes](https://llvm.org/doc/Passes.html),
[GCC optimization flags](https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html).

### X3 (2026-11-11): extended Linux runs

The combined working copy passes 1807/1815 vendored run tests on x86
(134 filtered); what remains are eight GCC optimization cases. Own tests pass
100/100. ARM/QEMU passes 1822/1822, own tests 105/105. Nonlocal jumps,
FP register spills, wide vector lanes, frame depth and 128-bit overflow results
for operands of at most 64 bits have isolated unity/non-unity builds and
targeted tests on both targets. GCC's ABI generator has a remaining
x86 failure out of 777 clang-valid cases; LLVM's generator passes 1000/1000.
This moves X3 but is not an approval of the remaining steps.

### X3 (2026-11-11): x87 operands in inline asm

- `t`/`u` operands are pushed on the x87 stack after the expressions have been
  evaluated; outputs are stored with their proper width and remaining inputs are
  popped. `st(1)` in the clobber list describes the pop that the asm template
  itself does. Mixed x87/GPR operands and other x87 constraints are not yet
  supported.
- Matching GPR operands get different widths (e.g. 64-bit input and
  32-bit output in the same register), like clang.
- GCC `990413-2`, `20030222-1` and `stkalign` pass on x86. The latter two
  also pass on arm64/QEMU. `asm_x86_x87` runs sqrt for float/double/long
  double, fpatan, fsincos, read/write and truncation, 32 times to
  catch leaked x87 stack entries. Separate compilation of changed C files green.

### X3 (2026-11-11): over-aligned stack

- Local objects with alignment above 16 get a dynamically aligned frame base in
  `r12`; `rbp` keeps the incoming arguments and the return address. VLAs get
  separate padding and an aligned object pointer. Outgoing stack arguments
  are aligned to the arguments' largest alignment and the stack pointer is
  restored after the call.
- `stack_overalign`: fixed objects (64/128/256), VLA (256), recursion,
  stack arguments, variadic arguments and a 64-aligned struct by value
  pass on x86-64 and arm64/QEMU. Cross calls clang/vcc in both directions
  pass on both targets (`test/abi/stack_overalign`). cproc's 12
  alignment cases pass compilation on both targets. `vla_zero` also passes:
  the zeroing loop's byte count is rounded up to whole 8-byte words.
- Inline asm that explicitly uses `r12` in an over-aligned function gives a
  diagnostic for now: the register is used as the frame base.

### X3 (2026-11-11): GNU conditionals and debug assembler
GNU `a ?: b` converts the saved value to bool for the branch, also
when a is float/double/long double. DWARF uses portable
`.long`/`.short`/`.quad` instead of arm64-specific directives.
The regression cases run on x86; GCC `20121107-1.c` and `pr41679.c`
assemble, and `llvm-dwarfdump --verify` accepts the debug object.

### X6 (2026-11-11): empty compiler barriers
Empty inline asm without operands, with a `memory`/`cc` clobber, is preserved as
a barrier record without a machine instruction. Other asm forms remain.
GCC IEEE now gives 49 passed, no failures, 30 filtered on x86 Linux,
the same as arm64/QEMU. Regression: `asm_barrier.c`.

### X3 (2026-11-11): floating-point builtins
The walker now lowers ordered/unordered comparison builtins for float,
double and x87 long double, and constant-folds NaN/infinity values.
Runtime classification (`isnan`, `isinf`, `isfinite`, `isnormal`) uses
the bit patterns without floating-point exceptions. GCC IEEE: 48 passed, one
failure (empty inline asm with a memory clobber), 30 filtered. The regressions
`fp_compare_builtin.c` and `fp_classify_builtin.c` pass on x86;
the file-by-file build also passes.

### X3 (2026-11-11): void lvalues
GCC's `compile/20000701-1.c` crashed when a void lvalue gave
a memory read of size zero. The walker now evaluates the address and
frees its register without reading memory, also for `volatile void`.
`test/vcc/void_lvalue.c` also checks side effects in addresses and
conditional expressions. x86's own tests: 47/56 passed, against 46/56 before
the change; the nine earlier functional gaps remain.

### X3 (2026-11-10): in progress, in branch `claude/x86-x3-main` (PR jdahlin/vroom#11; #9 ended up in #4's branch after #4 was merged)
- Done: library builtins as calls; aggregates (initializers, copies, compound
  literals, members of rvalues); bit-fields; System V calls (classification,
  records as arguments and results, hidden result pointer, varargs with
  register save area); floating point (SSE for float/double, x87 for long double
  via memory slots, `st(0)` result, all conversions); `__int128` (16-byte
  objects via address, libgcc for `/ % <<` and floating-point conversion);
  builtins (bit operations, overflow via 128 bits, `alloca`, `constant_p` without
  inlining, `signbit` ...); VLA (sizes in slots, the array below sp, sp restored
  at block end and when a `goto` reaches the declaration again); TLS (local-exec,
  initial-exec, general dynamic under `-fPIC`); PIC/PIE (GOT load via r11
  according to arm64's `sym_needs_got`, `@PLT` calls); atomics (`__atomic_*`/
  `__sync_*` via `LO(atomic)`: mov, xchg, lock xadd, lock cmpxchg, mfence;
  and/or/xor and the compound assignments of `_Atomic` objects as a CAS loop in
  the walker); complex numbers (`N_CPLX`; `_Complex long double` in st(0)/st(1));
  GNU vectors (via address, lane by lane in the walker; SysV classes as clang,
  SSEUP in one xmm, 32 bytes returned in xmm0:xmm1 as clang without AVX); label
  addresses (`.Ltmp.lbl.N` like the data initializers) and computed `goto`;
  cleanup variables (block end, `break`/`continue`, `return` with the value
  parked, `goto` out of scopes via a tree from the prescan, the `for`
  statement's first clause); alignment above 16 (locals from rbx = aligned
  frame, stack arguments via saved and aligned rsp); bit-fields across an 8-byte
  boundary (split at the boundary); float spill as for integers; `__float128`
  (via address, one xmm in the ABI, libgcc's `__addtf3` ..., `__*tf2`,
  `__extend*`/`__trunc*` also x87, `__float*tf`/`__fix*tf*`); builtins in the
  walker for all CPUs: clrsb, bitreverse, isnan/isinf/isfinite/
  isnormal/isinf_sign/fpclassify on the bit pattern (all floating-point types),
  isgreater ... isunordered, inf/nan/huge_val, atomics nand/max/min,
  test_and_set, clear; inlining in the walker (`always_inline` and small
  `inline`, arguments parked before binding, labels and case per copy, prescan
  per copy), constant locals and parameters, `__builtin_constant_p` after
  inlining; `-mgeneral-regs-only`/`-mno-sse`/`-mno-sse2`/`-mno-mmx` (macros as
  clang, no vector registers, floating point an error; `vcc-x86.py --fpcheck`);
  UTF-8 names quoted; inline asm for `-S` (X6's part in the emitter: AT&T,
  constraints, modifiers, x87 `t`/`u`, `asm goto`); `__builtin_setjmp`; dead
  code after jumps and in constant `if` as clang -O2; vectors of
  `__int128`/`long double`.
- `bench/vcc-x86.py`: 1768/1776, the same in `--pie` and `--pic` (vcc side
  `-fPIE`/`-fPIC`, linked `-pie`). Remaining: the run failure `20030125-1`
  (clang's `floor` -> `floorf`) and 7 link failures where clang -O2 removes a
  `link_error` call (X4's pass). `vcc-abi.py --target x86_64`: 12/12, with three
  new tests (`int_records`, `fp_records`, `complex_args`).
- Remaining in X3: bit-fields wider than 64 bits, 128-bit overflow builtins with
  128-bit operands, `need_fold` (an error() call left after inlining),
  `VCC_WALKLOG`.

#### Historical status in the local branch before the integration

>   scopes via a tree from the prescan, the `for` statement's first clause).
> - `bench/vcc-x86.py`: 1659/1770, the same in `--pie` and `--pic` (vcc side
>   `-fPIE`/`-fPIC`, linked `-pie`). Run failures only where clang's optimizer is
>   required (`20030125-1`, `bcp-1`, `builtin-constant`) and 11 link failures of
>   the same kind or `__builtin_setjmp`. `vcc-abi.py --target x86_64`: 10/12, with
>   three new tests (`int_records`, `fp_records`, `complex_args`) that pass all
>   four combinations; the rest stops at alignment > 16 and bit-fields over 8
>   bytes.
> - Remaining in X3: explicit `r12` in inline asm with an over-aligned frame base, `__float128` arithmetic (libgcc `__addtf3` ...), inlining of
>   `__always_inline` with `need_fold` and `constant_p` after inlining,
>   `-mgeneral-regs-only`/`-mno-sse`, `VCC_WALKLOG`.

### X2 (2026-11-10): started, in branch `claude/x86-x2-walker`
- The layers exist in minimal form: `codegen/rec/rec.h` (the record header,
  `opdesc`), `codegen/walk/` (the walker: expressions with constant folding,
  lvalues, conditions as jumps, statements, switch as a comparison chain, the
  call sequence; `walk.h` with the `LO()` interface), `arch/x64/codegen/` (`lower.c`:
  physical registers, `div` and shifts save their fixed registers, calls save
  live temporaries; `print.c`: `twoaddr` and the AT&T printer). `codegen/tu.c` picks
  the walker for x86-64, `data.c` writes `.long`/`.quad` and x87 bytes. `-c` for
  x86-64 is an error until X5.
- `bench/vcc-x86.py`: 777/1768 passed (gcc 696, tcc 62, vcc 19), no run
  failures; the rest are rejected with "not supported yet (PLAN-x86.md X3)"
  (builtins, aggregates, floating point, bit-fields, varargs ...) and 5
  `link_error` tests that require clang's -O2 folding. Under ASan/UBSan 774: the
  three differences are bugs in the front end that exist for arm64 too
  (`frontend/c/record.c:136`, `frontend/c/expr.c:36`, `frontend/c/func.c:7`).
- Remaining in X2: the op table filled in for x86 and
  `VCC_WALKLOG`/`vcc-walkcmp.py` (only needed when the passes and the inlining
  come, X3/X4).

### X1 (2026-11-10): front end done, x86 data and vcc-ldconst wait for the code generator
- `PP_LINUX_X86_64`: `--target=x86_64-*`, and the default without `--target` on
  an x86-64 host (glibc's `x86_64-linux-gnu`). Clang data in separate files from
  `tools/gen-x86data.sh` (clang 23.1.6): predefines and `-dD` order
  (`frontend/pp/ppdata-x86.inc`), `__has_*` (`has_tab_x86`) and attribute names
  (`frontend/c/attrnames-x86.inc`). The other targets' tables (23.1.4) are not touched.
- `va_list` as `struct __va_list_tag[1]`; `long double` as x87 in constants and
  folding (`include/common/f128.h`: binary128 layout rounded to 63 bits, `q_*p`);
  `__float128` as its own type (`TY_FLOAT128`, suffix `q`). `char` signed,
  `wchar_t` int. glibc's header files pass hosted.
- Verified: `-dM` = clang for 29 flag sets, `-dD` order for 17;
  `vcc-bflayout` 6604 claims without rejection; `bench/vcc-f80.sh` (x87 folding
  against the host's FPU) 16.3M checks without failure; `long double`/
  `__float128` folding and `va_list` diagnostics as clang's; `-fsyntax-only` over
  1903 test files as clang except where aarch64 deviates in the same way. arm64
  unchanged: `vcc-same` (the only difference: the path to the base copy's include
  directory), `vcc-ldconst` 6000/6000.
- Remaining for later steps: `vcc-ldconst` and `vcc-sema.py layout` for x86-64
  (need `-c` and an x86 kernel tree respectively), x87 bytes in data (`q_to_x87`
  exists), inline asm for x86 (X6), `vpp -dM` for `-march` deltas (`-msse4.2`,
  `-mavx2`, ...).

### X0 (2026-11-10): builds and runs, SIMD lexer and kernel parity remaining
- `make -C src vcc` on x86-64 Linux with clang 22: `classify`, `find_cend`,
  `find_nl` and `find_quote` in `frontend/lex/lex.c` have a scalar version without NEON
  (the same masks bit for bit), CRC32C via SSE4.2 (`include/port/hostcrc.h`, same
  polynomial), `pause` in the spin loop, `-march=native -msse4.2`. The fuzz
  targets take the host's clang and libFuzzer (LeakSanitizer is on by default on
  Linux: `ASAN_OPTIONS=detect_leaks=0` for the regress run, since it reports
  leaks already on empty input).
- Verified: `fuzz-regress` green; `fuzz-lex` ~1M runs (the window path with
  scalar masks against the reference lexer) without failure; `-S -O2`, `-c -O2`,
  `-S -O0`, `-E` of the vcc, gcc-torture and tcc suites (1903 files)
  byte-identical between the x86-64 build and an arm64 cross build (NEON) run in
  qemu-aarch64.
- SIMD classification completed 2026-11-13 (see X0/X5 update below). Remaining:
  `vcc-diff.py cmp` and `vcc-ctests.py` here (no `work/` tree; qemu-system-aarch64
  missing).

Other steps not started. The layer division (walker, lowering, common
passes) decided 2026-11-10; arm64 and wasm are moved later (TODO.md).

### 2026-11-11: function targets and generated x86 feature model

- CPU and feature selection (`-march`, `-m`/`-mno-`, `target("...")`) now share
  a static LLVM/Clang-23-generated model with dependencies and macros. `native`
  reads CPUID/XGETBV on an x86 host; no LLVM/Clang is needed at run time.
  Explicit negative flags and the order between feature flags are preserved.
- Function attributes are saved, the declaration cache is keyed by the feature
  mode and inlining requires a compatible target. An incompatible `always_inline`
  gives an error at code generation. SIMD and variadic register rules use the
  function target.
- CPU/feature acceptance does not mean that all instructions or ABI variants
  are implemented. YMM/ZMM calls are missing: vectors larger than 16 bytes as
  argument/return with AVX now give an explicit error instead of a wrong ABI,
  also for a direct call to an external function with an AVX attribute.
- Validation: `bench/vcc-x86features.py` compares complete `-dM` results
  with clang (CPU, feature order and native; synthetic CPUID/OS tests included).
  `bench/vcc-x86target.py` compares attributes/inlining with clang and checks
  the ABI errors. Both run in the ordinary premerge gate also on an ARM host.
- The ABI guard also covers struct/union with wide vectors, including
  nested fields and array wrappers. Clang's merging of classes controls the
  guard; large integer records and records that still pass in memory are
  allowed. 207 comparisons against Clang's LLVM signatures (SSE2/AVX/AVX512,
  global flags, function attributes and external targets) and 22 x86 ABI cases
  pass.

### 2026-11-11: review of nested targets and the variadic AVX ABI
Inlining now checks the feature state of the active inlined caller, not just
the function's outermost target. Eight oracle cases and eight verified cache
paths (declaration snapshot and replay with DRCHECK, respectively) pass.
Unnamed variadic wide vectors/records go via the stack and va_arg is allowed;
named YMM/ZMM parameters and return values are still blocked.
The natural alignment of x86 vectors is 32/64 bytes even without AVX, while
ARM64 keeps the 16-byte limit. 42 variadic compile/block cases, 18 cross calls
with clang and 12 layout comparisons pass in unity and file-by-file builds.

### OPT3 integration with X5 (2026-11-11)

Register locals use the same csslot/CFI as asm. Private asm snapshots
preserve the current homes on all edges; real unwind tests recover the
caller's callee-save values through ordinary and aligned frames (O1/O2).
See OPT3's PR status in PLAN-fas2.md; full O2 and the performance gate remain.

### 2026-11-11: integration of function targets, packed bool ABI and OPT3

PR39's function targets are kept in the merge with `9d022c3` (PR38/PR40).
The ABI classification keeps both named/unnamed arguments and the lane
transport of packed bool vectors; indirect variadic calls use the walker's
information about fixed parameters. Register locals still get their own
homes, while inlining keeps the active function's feature state.
SSE4's blend/string entries now describe their implicit registers and flags
for the shared optimizer. Regression tests check all four string variants
and fixed bool vector parameters via function pointers.

Unity and file-by-file build: own Linux tests 176/176 x86 (five reference
skips), 181/181 ARM; ABI 32/32 on both targets; diagnostics 220/220; sema
887/887 in process and batch mode. OPT rules O0-O3, register homes/setjmp,
unwind/CFI, ASan/UBSan entry effects and the interrupt tests pass. This is a
correctness integration; no new performance gain or closed release gate is
claimed.

Extended integration gates: 587/587 feature macros and 124/124 SSE4
acceptance cases in both build modes; kernel-PP 881/881 in process and batch.
Wasm32/64 run the asm-renamed math test 1/1 in both build modes.
The first Wasm start lacked the Emscripten configuration and did not reach
vcc; a rerun with an explicit SDK configuration is green. TSan passes 44
compilations on 11 source/target combinations with 1 and 6 emitter threads
respectively, identical assembler and no reports. Exact commands and binary
hashes are saved in `work/results/pr39-merge-validation/`.
### OPT4 copies and canonical unsigned results (2026-11-11)

A separate copies.c uses the shared native core and requires a local
overwrite before a copy can disappear; the two-address walk also handles
removal/debug lines. extensions.c proves from the previous entry when the
high bits are already zero. No new full passes/allocations/SSA.
Targeted O0-O3 runs, debug/unwind and sanitized contracts green;
the performance profile and its limitations are at OPT4 in PLAN-fas2.md.

### OPT4 leaves without a frame (2026-11-11)

`arch/x64/codegen/frame.c` now decides whether a truly frameless leaf function
can omit the rbp prologue/epilogue. The decision follows the existing
two-address walk: no new full passes, allocations or AST walks. Zero frame
bytes and no adjustment, canary or call marker are required first; the effect
description of each remaining entry must then lack rbp/rsp dependencies and
calls. Unknown effect and raw asm close the path. Stack arguments,
frame/return-address, setjmp, alloca, callee-saves and aligned frames keep the
current prologue. CFI uses the usual initial CFA rsp+8; debug lines, endbr,
patchable nop entries and the return thunk are left in place.

The driver interprets `-fomit-frame-pointer`/`-fno-omit-frame-pointer` in
order. Default: allowed at O1-O3, O0 keeps the frame; explicit omit also
allows O0 (verified against clang 23). VCC_NOOPT keeps the frame. The policy
comes before `cc_opts.outfd`, so the declaration cache's existing option
signature includes it. Other targets' frame choice is unchanged. The function
attribute optnone has long been ignored in the parser; this delivery
introduces no new attribute semantics.

Validation: `bench/vcc-x86-leaf-frame.py` passes levels/flag reordering,
mixed header caches with and without replay, negative frame requirements, code
and execution, patch entry, CFI and dwarfdump. A signal interrupts a truly
call-free, frameless leaf function; libgcc finds both the leaf and the caller
through its FDE. CI runs the host-independent part with `--no-run`. Own x86
suite: 215/215 with six reported reference exceptions; ABI: 32/32, four
clang/vcc combinations. Existing libgcc tests for register homes in
ordinary/aligned frames are green at O1/O2. Unity build and standalone syntax
compilation of the changed units green.

Bounded CPU cost: cold separate processes, Callgrind Ir, the same
`VCC_OPT='-O3 -march=x86-64-v3'`, target x86-64 O2. Base 781ca697's compiler
sources are hash-verified identical to 54580f7 (the only ccs difference is a
test file). Base binary SHA256
`6f42919ea1d4eef11100e017f2857b72054e52161620b443a1ada705bab89cf7`,
candidate `a01b54686639a58ffe897137851c8abc2fd6793be7533d1ccaa32923cca79672`.
`work/opt-profile/leaf-cost/report.json` contains commands, source hashes and
diff; `work/opt-profile/measure-leaf.py` recreates the two 501-function TUs.

| Corpus | Ir base → new | Difference | .text base → new |
|---|---:|---:|---:|
| Positive: `int fN(void){return 151+N;}` | 10 587 676 → 10 569 032 | −0.176 % | 16 022 → 8 014 bytes |
| Not applicable: `unsigned fN(unsigned x){return x*37u+(x>>3);}` | 16 471 567 → 16 383 053 | −0.537 % | 24 039 → 24 039 bytes |

Positive source hash `eee9910c8737d006d756893be7d52f6bb2e15c36bb39dbf929247aa15d3c362d`;
control `a939023c599b599a652f3a88ff6fb10aa6cbd2a5d40a1741cf02812063c67b9e`.
The control's assembler is byte-identical. The byte gain includes function
alignment, not just removed instructions. These two small synthetic TUs
measure neither program run time, a whole corpus nor any general compiler
improvement; G5/G7 are open.
The next separate quality gap is the ordinary stack homes of cold-used
parameters: the current direct ABI binding applies only to profitable,
selected register homes.

PR50 integration 2026-11-11: updated against 9b18f642 (main through 4bb6d52
plus PR49). Interrupt prologue/epilogue and new x86 function targets are
preserved; the frameless policy explicitly applies only to ordinary functions.
Unity -j1, the leaves' flag/cache/hardening tests including the interrupt
exception, real signal unwind through a frameless leaf, existing
ordinary/64-aligned unwind O1/O2 and debug lines O0-O3 pass. CI for the
integration head remains.

### 2026-11-12: VIR's first x86-64 lowering

`VCC_VIR=bl -O2`: allocated SysV leaf functions and the shared loop
vectorizer's SSE2 part now work on x86-64 too. Details, remaining
retreats, targeted tests and measurements are in `PLAN-vir.md`,
V2/V4 status 2026-11-12. No change to the default choice or -O0/-O1.

### 2026-11-12: VIR with calls and 82 TSVC vector loops

VIR now lowers SysV calls, FCMP/select, packed compare and mask select, SSE2
i32 multiplication and signed vector conversions, division/remainder and block
memory.
TSVC: 151/151 checksums, 157/158 built functions lowered and 82/82
vector candidates in machine code (previously five functions). SQLite, zlib,
PostgreSQL/psql and CPython are tried; the named remaining builtin/inline gaps
of the last two are kept visible. See PLAN-vir.md V2/V3 and TODO T13.
New C tests and the assembler's INT64_MIN test pass in unity/nonunity and with
sanitization; the default choice and -O0/-O1 are unchanged.

### 2026-11-12: first profile-driven VIR slice after PR126

SQLite main, three short perf-stat tests: CPU time 1.897x → 1.386x clang -O2;
instructions 2.560x → 1.626x, verification hash unchanged. Atomics, switch
with relative tables, mulhi, immediates, compare+branch and simple folded
addresses; details, known test exceptions and reproduction in PLAN-vir.md V4.
The goal of at most 10 % slower is open; indexed memory operands come next.

Indexed memory operands are now lowered with scale 1/2/4/8, without a separate
ptradd/shift when single-use values and allocator edits allow it. SQLite:
1.626x → 1.603x instructions/clang, CPU unchanged within the short test's
noise (1.380x → 1.378x). The 10 % goal is still open; V4 in PLAN-vir.md has
the test data.

VIR's shared size-driven inliner is now active on x86 too. Together
with fallthrough for true edges, fewer identity copies and tied preferences
it gives, in five short SQLite tests, CPU 1.360x → 1.267x clang and instructions
1.603x → 1.505x. Verification hash and regression tests match; 10 % is open.

After small block copies, folded flag tests and a stack-relative VIR frame,
SQLite main is at 1.189x clang -O2 in five short tests (instructions 1.389x).
rbp can now be allocated when the frame pointer is omitted; CFI, stack
arguments and preservation of rbp are run-tested. Own 305/305, GCC/TCC
1615/105, the same five inline exceptions; TSVC 151/151 and 82 vector loops,
unity/nonunity and ASan/UBSan green.
See V4 in PLAN-vir.md; at most 10 % slower is still open.


### 2026-11-13: shared VIR layout from ARM

Block chasing, loop rotation with a copied entry test and small join copies
are now planned in `vir/layout.c`, the same code for ARM and x86. ARM's SQLite
output is byte-identical; x86 gets about 0.6-0.9 % lower CPU time in two
five-test series of speedtest1 size 100. Latest series: 1.154× → 1.148× clang
-O2; instructions 1.382× → 1.364×. The goal of at most 1.05× is still open.
VIR 130/130, own x86 307/307, GCC/TCC green, the same five known inline
failures; TSVC 151/151 and 82 vectorizations. Focus tests also run ARM's
existing rotation/join cases, ASan/UBSan and file-by-file build. Details in
PLAN-vir.md.


### 2026-11-13: r10 between switches

The shared VIR allocator now preserves live clobber registers also at
conditional terminators. x86 declares the switch's r10 as an early clobber and
does not need to reserve it throughout functions with a sparse/multi-use
switch.
Two short five-test series gave about 0.5-0.6 % lower CPU time for SQLite main;
1.141× clang in the second series. A synthetic test reproduces the old
allocator bug and sweeps 48 register configurations. See V4 in PLAN-vir.md.


### 2026-11-13: small SQLite decoders

The shared `vir_read_be` recognizes exact 16/32/64-bit byte reads,
also with constant offsets. x86's vdbeRecordCompareInt goes 102 → 82
instructions, sqlite3VdbeSerialGet 138 → 122 and walDecodeFrame 95 → 71.
Function times are layout-sensitive; two five-test series of the whole
speedtest1 are neutral, about 1.148× clang. Fewer instructions do not count as
reaching the 1.05× goal. Details and rejected flag/table attempts in PLAN-vir.md.


### 2026-11-13: compares directly in memory

VIR's ordinary single-use loads can become cmp with exact memory width, also
indexed. Narrow promotion cases require a representable ==/!= constant;
volatile and multi-use loads are not combined. Instructions/clang in
SQLite main 1.358× → 1.344×; CPU time varies between series (1.141-1.154×).
The 1.05× goal is open. Own x86 308/308, the same five known inline failures,
ASan/UBSan, file-by-file build and TSVC 151/151 with 82 vector loops green.
See V4 in PLAN-vir.md.


### 2026-11-13: main PR127-129 and scale through pointer cast

Rebase onto 5a111ce5. The shared block planner also keeps main's new
rule for an operation followed by br; ARM and x86 use the same proof.
x86's index scaling is now folded also through add.i64+i2p. In pcache1Fetch,
imulq+load with scale 1 becomes a load with scale 8. Two five-test series give
1281/1279 → 1249/1250 ms CPU, 1.124× clang; about 2.3-2.5 % gain. The 1.05×
goal is still open. A proposed change to 32-bit ABI carriers was rejected
as neutral; SPEC's O1 is unchanged. Details in PLAN-vir.md.


### 2026-11-13: preserve NSW and share the index proof with ARM

VIR keeps a signed multiplication's NSW even when it becomes a shift.
The shared vir_sext_shift proof is used by ARM's extended-register
addresses and x86's scaled memory operands. In BtreeIndexMoveto, movslq +
a read with scale 2 suffice; shll disappears. Two five-test series give about
0.5-0.8 % CPU gain, 1.120×/1.126× clang. Instructions 1.329× → 1.322×.
The 1.05× goal is still open. The attempt to reorder SSA before
allocation was rejected; only the legality proof is shared, without changed
live ranges. Validation and measurement data in PLAN-vir.md.
Validated: own x86 315/315 and the same five known inline failures, ARM's 46
VIR tests, ASan/UBSan and file-by-file build green. TSVC x86 151/151 and **83**
vector loops (previously 82, s1111 added); ARM/QEMU 151/151 checksums.


### 2026-11-13: signed four-byte decoder

The shared vir_read_be can also combine a signed first byte when a
24-bit i32 shift discards the sign extension. SQLite SerialGet goes
122 → 114 instructions; fast-aligned four-byte test 31.441 → 28.363 ms
(−9.8 %), clang 28.225 ms. The whole speedtest1 improves variably 0.1-1.1 %
in two short series, so no stable whole-program gain is promised. The 1.05×
goal is still open; details and measurement data under V4 in PLAN-vir.md.


### 2026-11-13: private switch selector and dead clobbers

A private dense selector can be produced directly in r10 instead of being
copied there. The form computation is shared between target requirements and
dispatch. The allocator does not preserve dead operands in the terminator's
clobbered registers; live values are preserved as before. vdbeRecordCompareInt
loses a movl.
Two five-test series of an identical SQLite binary give 0.5-1.1 % lower CPU
time; 1.131×/1.118× clang. The 1.05× goal is open. The broader phase-3 idea
was rejected by random seed 17; the narrow rule passes 30 seeds/240 functions.
Details under V4 in PLAN-vir.md.
Validated: own x86 315/315 and the same five known inline failures; ARM/QEMU
46/46, ASan/UBSan, file-by-file build and the allocator's 30 random units
green. TSVC 151/151 checksums, **83** vector loops remain.


### 2026-11-13: widening of already-wide indexes

Ordinary ext and NSW index scaling now share the same physical 32→64 proof.
getCellInfo loses a movslq after its halfword load. A dependent index test goes
81.389 → 75.971 ms (−6.7 %); the whole speedtest1 is neutral in the
repeat, 1.125× clang, so the 1.05× goal is still open. The regression
also holds 13 signed indexes across a call: eight four-byte spills must
keep the sign extension. Other attempts with GVN, spill placement and real
frameless sibling calls gave no certain whole-program gain and are removed;
measurement data and lessons under V4 in PLAN-vir.md.
Validated: own x86 315/315 and the same five known inline failures, ARM/QEMU
46/46, ASan/UBSan and file-by-file build green. TSVC 151/151 and **83** vector
loops.


### 2026-11-13: shared context-sensitive inlining

Constant arguments to referenced parameters can give a VIR budget of 600
instead of 150 AST nodes. Only `(void)param` does not count. The prepass's
bottom-up size includes both these places and single-use bodies, so a
wrapper is not wrongly counted as small after inlining. The rule is shared
with ARM; O0/O1 unchanged. Three five-test series on speedtest1 main give
0.5-1.1 % gain, 1.113-1.115× clang. The 1.05× goal is still open. 450, 750 and
a bonus only within loops gave no gain and are not kept; data under V4
in PLAN-vir.md.
Validated: own x86 316/316, GCC 1615/1615, TCC 105/105, the same five known
inline failures; ARM/QEMU 47/47, ASan/UBSan and file-by-file build green. TSVC
151/151 and **83** vector loops, prefix check 134 names.


### 2026-11-13: measure the small functions' remaining whole-program contribution

New `bench/sqlite/vir-x86-swap.py` measures clang's assembly for RecordCompareInt,
btreeParseCellPtr, getCellInfo and pcache1Fetch in temporary VIR hybrids.
2048-byte text slots and a check of all 1478 text symbol addresses keep
other code at the same address. All verification hashes match. The first
five-test series wins only 0.5 % with all four swapped; a third short series
confirms 0.8 %. The intermediate repeat has clear load variation. Even the hybrids are above 1.05× clang, so the next focus is
the VM and larger search routines. No new optimization code is kept from this
round; prototypes with fewer copies/tests were neutral or slower.
Full measurements and rejected attempts under V4 in PLAN-vir.md. The restored
compiler passes the focused x86 rigs and VIR's self-tests.


### 2026-11-13: one-time bonus limited in large callers

The shared inliner keeps the ordinary/context-sensitive budgets when a
caller's own AST is already larger than the one-time bonus's 4000 nodes. Thus
an already large allocation unit does not grow merely because the helper has
one reference. The walker and the bottom-up size model make the same decision.
SQLite VdbeExec 24854 → 17295 instructions, 119382 → 82806 code bytes and
2680 → 1800 bytes of frame. Three calmer speedtest1 series give 1.1-1.7 % gain,
1.099-1.104× clang; contended series vary strongly and are also reported
under V4 in PLAN-vir.md. **The 1.05× goal is still open.**
Validated: own x86 317/317, GCC 1615/1615, TCC 105/105 and the same five known
inline failures; ARM/QEMU 48/48, focused ASan/UBSan and file-by-file build green.
TSVC 151/151 and **83** vector loops; prefix check 134 names. The final build's
SQLite binary is byte-identical to the measured candidate.


### 2026-11-13: narrow local fields with different integer carriers

Simplification can remove a trunc before a narrow store. Slot promotion in the
shared builder now normalizes i64/i32 carriers by the field's memory width,
so mixed store paths do not leave a local on the stack. The clang comparison
pointed to serial_type in vdbeRecordCompareString, which is now kept in a register.
Two five-test series of speedtest1 give 0.92-0.98 % CPU gain, **1.094-1.096×
clang -O2**. Dynamic instructions 1.272× → 1.263× clang. The 1.05× goal is
still open; measurement data under V4 in PLAN-vir.md.
Validated: own x86 318/318, GCC 1615/1615, TCC 105/105, the same five known
inline failures, ARM/QEMU 49/49 VIR tests, focused ASan/UBSan and file-by-file build.
TSVC keeps 151/151 checksums and **83** vector loops. The rule is shared
with ARM, whose native performance has not been measured.


### 2026-11-13: rejected range and dead-store attempts

Dominating sign ranges remove division bias in
vdbeRecordCompareString but give 1.1-1.4 % worse speedtest1 CPU in two
five-test series. Local dead-store cleanup after load forwarding removes only
69 static instructions in the amalgamation and gives about 0.7 % worse CPU.
Both prototypes and temporary promotion traces are restored;
focus tests and verification hashes were green. Measurement data under V4 in
PLAN-vir.md. The kept compiler remains at 1.094-1.096× clang
in the latest comparable series; the 1.05× goal is open.


### 2026-11-13: ARM CI's debug line for raw instructions

The new dispatch test exposed that the assembler's `.inst` let a pending
`.loc` land after the instruction. The line is now consumed before the code
bytes, as for ordinary mnemonics. Direct assembler regression added. Full
DWARF rig 1591/1591 and assembler objects 62/62, also ASan/UBSan and
file-by-file build, green. The SQLite binary is unchanged against the latest
measurement build; the 5 % goal is still open.

### X0/X5 (2026-11-13): compilation speed and portable host dispatch

X0 now has runtime-selected AVX-512 VBMI/BW, AVX2 and scalar lexer paths.
The CPU detector also checks OS support for vector state. CRC32C selects
SSE4.2 or an identical software implementation once at process startup.
The default host build is baseline `-march=x86-64 -mtune=generic`;
`HOST_CPU=-march=native` remains available for machine-local builds in a
separate output directory. `VCC_LEX_SIMD=scalar|avx2|auto` caps selection,
and `VCC_CRC=software` exercises the fallback without enabling unsupported
features. ARM's NEON and ACLE paths are unchanged.

X5 steps 1–2 and shared data replay are implemented. X86 `-c` now passes
compact records and structured operands to the existing assembler encoder,
relocation logic and branch relaxation. The item stream, data objects and
symbol replay have moved from `arch/a64/codegen/obj.c` into `codegen/obj.c` and
are shared by ARM and x86. Inline assembly, prologues, epilogues, CFI/DWARF
directives and unconverted instruction records remain text snippets in the
same session. `-S` is unchanged; `VCC_OBJ_TEXT=1` retains the full text path
for comparison. Further instruction conversions should follow profiling.

Assembler layout now uses dense fragment ranks and Fenwick sums instead
of repeatedly walking preceding fragments during relaxation. Fixed-layout
CFI and DWARF offset queries reuse a fragment-base snapshot.

Measured on the shared Ryzen 7 9700X host, using SQLite's 9.28 MB
amalgamation, with no GCC/Clang compile-speed comparison:

- The assembler indexing change reduced direct x86 compile CPU time from
  9.005 s to 0.292 s, and VIR from 28.315 s to 0.571 s, preserving objects.
- Native SIMD lexing took 4.9 ms versus 30.5 ms scalar; AVX2 took 5.9 ms.
  The final portable runtime-dispatched build took 6.0 ms automatically
  and 6.7 ms with AVX2 capped selection.
- Direct object emission, seven alternating process pairs using the same
  pinned compiler: median CPU 442.7 → 391.4 ms (11.6% less) for direct
  code generation and 904.6 → 833.9 ms (7.8% less) for VIR. These runs had
  substantial concurrent load and are not comparable to earlier absolute
  timings. Objects matched with debug info, PIC, separate function/data
  sections and patchable function entries.

Original checkout validation: 2,213 byte-identical x86 object comparisons
against Clang, 322 focused comparisons with VIR and ASan/UBSan, 319 native
x86 executions and 326 ARM tests (one existing xfail). Raw lexer parity
passed on 329 files in each runtime mode; sanitizer fuzzing passed against
the scalar oracle. Runtime dispatch passed all six CRC/lexer combinations
on the host and emulated Conroe, Nehalem, Haswell and XSAVE-disabled
Haswell. Fast dispatch and direct-object comparisons are in premerge.
The isolated PR branch is revalidated separately after integration.
Full kernel parity remains outstanding.

Raw measurements and validation logs are in
`results/sqlite-compile-20261113.json`, `results/lexer-simd-20261113.json`,
`results/host-dispatch-20261113.json` and `results/x64-direct-20261113.json`.

PR integration validation on current main: clean build, **321/321** focused
x86 direct-object comparisons (five existing input skips), **63/63** assembler
object fixtures, and all six runtime-dispatch combinations on the host and
four emulated CPU configurations. The one-test difference from the original
checkout reflects the unrelated VIR regression kept out of this PR.

### X6 (2026-11-13) — FFmpeg 8.1.3 på Linux x86-64

`CLANG=clang-23 bench/ffmpeg/linux-x86.sh` bygger den oförändrade
utgåvan i två träd och kör FATE utan externa testdata. I det första
bygger NASM de fristående assemblerfilerna och vpp hanterar inline asm.
Det andra använder `--disable-asm` för att köra DSP-kodens C-varianter.
Make använder vpp för CC och HOSTCC, även för testverktygen. Clang kör
konfigurationsproberna och anropar systemlänkaren.
