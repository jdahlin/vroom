# VIR (Vroom IR): the SSA level on top of the walker

**Goal:** a small, fast IR in SSA form for what cannot be done without SSA:
real register allocation, global value numbering and store-to-load forwarding
across blocks.  It is to be used by x86-64 and arm64 (wasm later), cost
nothing at `-O0` and not repeat LLVM IR's known mistakes.  The format and the
tools are specified in `SPEC-vir.md`; the background is in
`RESEARCH-vir.md` (LLVM IR's problems with sources, and what B3, Cranelift,
Zig, Go, Turboshaft, LuaJIT, QBE and TPDE chose instead).  Register
allocation has its own background, `RESEARCH-regalloc.md`.

**Decisions (2026-11-11, round of questions with Johan):**
- `-O0` keeps today's direct code generation.
- The semantics are defined as standard (wraparound, no type-based alias,
  null is an ordinary address), with stricter rules as flags one turns on.
- x86-64 and arm64 are both targets.  (The first round of questions said
  "arm64 first"; it was based on a branch that was 566 commits behind `main`
  and lacked x86-64.)
- VIR exists **only at `-O2`**.  `-O1` remains OPT0-OPT8 without SSA.

## Starting point (2026-11-11, `main` 8bdc940 series + x86)

- Three code generators are selected in `codegen/tu.c`: arm64 (`arch/a64/codegen/`,
  its own AST generator), x86-64 (the shared walker `codegen/walk/` + the
  lowering `arch/x64/codegen/` through `LO()`), wasm (`arch/wasm/codegen/`, its
  own).
- `PLAN-fas2.md`, **Shared optimizer without SSA** (OPT0-OPT8), owns the
  optimizations that work without an IR: algebra rules in `codegen/opt/`, post
  passes in `codegen/rec/`, block graph and liveness in `codegen/flow/`.  TODO.md
  says explicitly "No new IR/SSA".
- `PLAN-x86.md`, "What cannot be done without SSA", points out the way if more
  is needed: an optional SSA pass with virtual registers, only for `-O2`.
  **VIR is that pass.**  So this plan does not change OPT0-OPT8 but adds a
  level above; the TODO line needs to be made precise when V1 starts.
- Values in the walker are physical registers handed out by the target
  (`LO(treg)`), with spills around calls and a restart of the function when
  the temporaries run out.  There is no register allocator on any target.
- Measured on arm64 (PLAN-fas2 status 2.6, 80 kernel TUs, static): vcc 1.51x
  clang -O2; clang's backend + mem2reg/SROA alone 1.15x.  The difference is
  mainly values in registers across the whole function.

## Design

### Where VIR sits

```
frontend/c/'s AST ──▶ walker (codegen/walk/) ──LO()──▶ x64_* / a64_*  ──▶ records (codegen/rec/) ──▶ code
                     │                              ▲
                     └──LO()──▶ vir_*  ──▶ VIR ──▶ passes ──▶ register allocation ─┘
```

- **VIR is a virtual target for `LO()`.**  The walker is compiled a second
  time with `LO(name)` → `vir_##name` (the template PLAN-x86 already prepares
  for a second CPU).  All C-level decisions (folding, reachability, inlining,
  `__builtin_constant_p`, switch strategy, cleanups, the `codegen/opt/` rules)
  are then made by the same code as today.  There is no fourth copy of the
  C rules.
- `vir_treg` hands out a new variable number and never runs out; `vir_tfree`
  does nothing.  Each `LO` operation reads its variables as SSA values and
  writes a new definition (Braun et al. 2013: SSA is built while the
  operations arrive, labels with backward jumps are sealed when the function
  is done).  Locals without `lnoreg` become variables through `LO(local)`;
  the others are stack slots as today.
- A VIR function is built **for a specific target**: `LO(classify)`, `ldmem`,
  `i128mem` and the data model are answered by the real CPU.  So VIR is not
  target-independent, no more than LLVM IR after clang.
- **Lowering:** after the passes and register allocation, the real CPU's `LO`
  operations are called with the assigned registers.  From there today's path
  applies unchanged: the `codegen/rec/` passes, `twoaddr.c`, frame and prologue,
  printer and encoder.
- **Fallback per function:** if VIR cannot build a function, it is generated
  by the ordinary path (same walker, real `LO`).  In 1.0 this applies to:
  `returns_twice` calls, VLA/`alloca`, `__int128`/`_BitInt` in register pairs
  (`w*`/`big*`), x87 `long double`, vector types.  Coverage is measured per
  step.

### Representation

- Per function and thread, in `ecx`; nothing mutable is shared between
  functions.
- **Instruction, 16 bytes:** `op:u8, ty:u8, fl:u16, a:u32, b:u32, c:u32`.  A
  value is the index of the instruction that defines it.  Extra operands
  (call arguments, block arguments, switch cases, asm operands) are in
  `u32 extra[]`.
- Constants are instructions (`iconst`, `fconst`, `symaddr sym+off`,
  `slotaddr`, `labeladdr`), unique per function and without a place in any
  block.
- Blocks are contiguous ranges in reverse postorder, with block parameters
  (no phi nodes), loop depth and flags.  The graph is built with
  `codegen/flow/graph.c`.
- No use lists.  A pass changes in place (`nop`, alias table) or writes a new
  array in one forward sweep.
- Side tables per value: source position (`LO(dbgrow)`), a work cell for the
  running pass.

### Types

`i32`, `i64`, `f32`, `f64`, `ptr`.  `i8`/`i16` exist only as the width of
loads and stores.  Comparisons give `i32` 0 or 1.  Aggregates are never
values: they live in stack slots and are moved with `memcpy`.

### Semantics

- Integers wrap.  Shift counts are taken modulo the width.  `sdiv` of the
  minimum number by -1 wraps.
- Division by zero and access through an invalid pointer are **immediately
  undefined**: the operation may be removed if the result is unused, but not
  moved to where it did not run before.
- No `undef`, no `poison`.  Every value is a concrete bit pattern and memory
  is concrete bytes.  A variable read before its first write is 0.
- **Flags one turns on** (`NSW`, `NUW`, `EXACT`; later `INB` and a type tag on
  loads and stores): violating the flag is immediately undefined at that
  instruction.  A pass that moves the instruction clears the flag.  The
  builder sets `NSW` when the TU lacks `-fwrapv`/`-fno-strict-overflow`; no
  1.0 pass depends on it.
- **Pointers:** `ptr` is a type of its own.  `p2i` exposes the object, `i2p`
  may point at any exposed object, a round trip is never folded.  A pointer
  is replaced only by the same SSA value.  A stack slot whose address is only
  used as an address operand and in `ptradd` is non-escaped.
- Loads and stores carry width, extension, alignment and `volatile`.
  Volatile accesses are never removed, merged or reordered.
- Effects are a property of the opcode (pure, reads memory, writes memory,
  may not return), refined on calls to `const`/`pure`/`noreturn`.
- Floating point: round to nearest and invisible exceptions, unless the
  function has `fenv`; then floating-point operations are effects.  Folding
  never produces a NaN.  `nofp` (`-mgeneral-regs-only`, the kernel's
  `-mno-sse`) forbids floating-point values.

### Operations

They follow the `LO()` interface, so that the builder is a thin translation:

| Group | Operations |
|---|---|
| Constants | `iconst fconst symaddr slotaddr labeladdr tlsaddr` |
| Integer | `add sub mul sdiv udiv srem urem and or xor shl lshr ashr neg not umulhi smulhi` and the bit operations (`clz ctz popcnt bswap`) |
| Width | `sext zext trunc` |
| Comparison, select | `icmp fcmp select` |
| Floating point | `fadd fsub fmul fdiv fneg fsqrt cvt fbits` |
| Pointer | `ptradd psub p2i i2p` |
| Memory | `load store memcpy memset` |
| Atomics | `atomic` (WK_ALOAD … WK_AFENCE) |
| Calls | `call` (with `wkarg[]` from the target's `classify`), `param`, `proj`, `va_start`, `va_arg` |
| GNU C | `asm` (the constraint string is kept until allocation), `frame`, `barrier` |
| Terminators | `br brif switch ibr asmgoto ret trap` |

- `ibr` (computed goto) lists all of the function's address-taken labels.
  Those blocks have no parameters, so variables that live there are kept in
  stack slots.
- `asmgoto` edges are ordinary edges that may be split; outputs are results
  of the terminator.
- `trap` is always an instruction (`ud2`/`brk`): code never falls off the end
  of a function (objtool).

### Passes at `-O2`

1. **Build** (the walker through `vir_*`): SSA during the build, folding and
   local CSE on insertion.
2. **Simplify:** trivial block parameters removed, constants forward, folded
   branches, unreachable blocks.
3. **Redundancy:** value numbering over the dominator tree, store→load and
   dead stores on non-escaped stack slots, dead code.
4. **Allocate:** greedy assignment in block order on the SSA form, evicting
   the value whose next use is farthest away (SPEC-vir.md 15.5-15.11;
   integer and floating-point class, parallel moves per edge, fixed
   registers for `div`, shifts and arguments, hints instead of a coalescing
   phase).  No LICM in 1.0, with one exception (decision 2026-11-12): loop
   constants in straight innermost loops without calls are hoisted (`vv_pin`,
   `codegen/vir/vec.inc`).
5. **Lower** through the real CPU's `LO`.

The pass list is fixed: ordinary function calls, no pass manager, no
fixpoints without a budget (the same rule as `codegen/flow/`).

### Text form, checking and interpreter

- Printer and parser for a text form; `vpp -fvir-dump=STEP`.
- Verifier after every pass in test and sanitizer builds.
- `virtest` runs named passes on `.vir` files; snapshots in
  `src/test/vir/`.
- `virrun`, an interpreter, is the reference semantics.  Every pass is
  checked by interpretation before and after, on the tests and on random
  functions (`src/test/fuzz/fuzz-vir`).

### Files

- `src/vir/`: `vir.h`, `virimpl.h`, `print.c`, `parse.c`, `verify.c`,
  `interp.c`, `simplify.c`, `gvn.c`, `mem.c`, `regalloc.c`.  Its own module
  `vir_SRC`.
- `src/codegen/vir/`: `build.c` (the `vir_*` implementation of `LO`) and
  `lower.c` (VIR → real `LO`), in the `emit` unity TU.
- Names: `vir_*`, `VO_*`, `VT_*` (`V_CONST`/`V_REG` are taken).

## Steps

| # | Deliverable | Criterion |
|---|-------------|-----------|
| V0 | **Background and plan.** `RESEARCH-vir.md`, `PLAN-vir.md` | Reviewed by Johan |
| V1 | **Format and tools.** `vir.h`, printer, parser, verifier, interpreter, `virtest`, `fuzz-vir` | Write-read-write identical on all tests; the verifier rejects a set of mutations; default build byte-identical (`vbench.py check`) |
| V2 | **Builder and simplest lowering on x86-64.** The walker against `vir_*`; lowering where every value lives in a stack slot; `VCC_VIR=1`; fallback per function | With `VCC_VIR=1`: `bench/vcc-x86.py`, `vcc-abi.py --target x86_64`, gcc-torture the same passes as without; coverage reported per corpus; without the flag byte-identical |
| V3 | **Coverage.** Bit-fields, records in calls, varargs, atomics, floating point, switch (the walker's lowering), computed goto | Coverage ≥ 95 % of PostgreSQL's functions and of vcc's own; the x86 kernel boots with `VCC_VIR=1` (asm functions take the ordinary path); objtool without warnings |
| V4 | **Register allocation.** Next-use chains backward, greedy assignment forward, fixed registers, parallel moves per edge, `vir_alloc_check` | Instructions on a fixed set of TUs below today's `-O2`; allocator time and counters (spills, reloads, moves) reported separately; the check green on the tests and `fuzz-vir` |
| V5 | **Passes.** Simplification, value numbering, store-to-load forwarding, dead code | Per pass: interpreter equivalence, instructions and `vbench.py` time before and after, in the plan |
| V5b | **Inline asm.** `x64_asm` is split into planning and emission (SPEC X6); `asm` and `asmgoto` are built | Without the flag: the kernel objects and `vas-corpus.py` byte-identical; with it: coverage ≥ 99 % of the x86 kernel's functions |
| V6 | **arm64.** Requires arm64 to have an `LO` lowering (TODO.md "arm64 and wasm on the shared walker"); VIR drives the move or waits for it | ctests 1729/1729, `vcc-abi.py`, the arm64 kernel boots with `VCC_VIR=1`; `vcc-obj.py` 840/840 |
| V7 | **VIR is the default at `-O2`.** | Proposal: ≤ 1.25x clang -O2 in static instructions; phase 2 self-build ≤ 1.5x slower than the clang build (2.6x today); `-O2` compilation ≤ +15 % in total; `-O0`/`-O1` byte-identical all the way |
| later | `NSW` and type tags are used; SROA of non-escaped records; `returns_twice` with abnormal edges; VLA; `__int128`; wasm | — |

"VIR 1.0" is the format and semantics as they look when V7 passes.

V1 generator: `bench/virgen.py --seed=N --size=N` gives deterministic
integer programs with initialized stack slots and branches. 101 cases,
including maximum size, are verified and written-read-written under ASan CI.
This is a first part: floating point, calls, more control flow, `virrun` and
equivalence testing remain. Generated programs use no external calls.

## Validation

- Until V7, VIR is behind `VCC_VIR=1`; every commit leaves the default output
  byte-identical (`vcc-same.py`, `vbench.py check`).
- Two paths through the same walker are a test in themselves: the same
  program built with and without `VCC_VIR=1` must behave alike in the C
  suites, the ABI tests and the kernel boot.
- The interpreter compares before and after every pass; `fuzz-vir` with
  ASan/UBSan.
- Speed gate: `bench/vbench.py gate` with the flag off (±0.1 %) and on
  (reported).

## Risks and open questions

- **G5 measures `-O1`.** VIR is decided for `-O2`, so G5 (`-O1` within X % of
  clang -O1) must be reached by OPT0-OPT8 alone.  VIR's own gate is V7.
- **Changes in the walker and the x86 lowering** are reviewed against the code
  in SPEC-vir.md section 19 (X1-X9).  The largest are X6 (inline asm:
  `x64_asm` is split in two) and X1 (the walker is instantiated twice); calls
  with values in registers (X4) and a desired result register (X7) are needed
  only at V4.
- **`LO` is not pure.**  `wkarg` and parts of `walk.h` are SysV-specific.
  VIR inherits that until arm64 moves; V6 depends on that move.
- **Where x86-64 is tested.**  `bench/vcc-x86.py` runs on an x86-64 host.
  V2-V5 need that machine (or V6 taken first, which requires arm64 on `LO`).
- **Immediately undefined instead of poison** makes flagged instructions
  impossible to speculate without clearing the flag.  A deliberate limit.
- **Two builds of the walker** (real CPU and `vir_*`) increase the compiler's
  size and build time; measured in V2.
- **The rule "No new IR/SSA" in TODO.md** and the heading "without SSA" in
  PLAN-fas2 apply to OPT0-OPT8.  They are made precise ("at `-O0`/`-O1`")
  when V1 starts.
- **Open:** benchmark suite and X for G5 (PLAN-release).

## Status

### V4 x86-64 against clang -O2 (2026-11-12), goal at most 5 % slower


2026-11-13, dynamic VM review after `52b78073` (no compiler change):
A frozen copy of vpp and fresh SQLite binaries in `work/vir-vm-audit/`;
the VIR object is byte-identical to `promote-width-final`. Ryzen 7 9700X,
CPU 7, same SQLite 3.50.4, -O2, THREADSAFE=0 and OMIT_LOAD_EXTENSION as
above. Three alternating main samples, size 100: clang **1143.52 ms**, VIR
**1249.01 ms**, **1.092x CPU**, **1.094x cycles**, **1.263x instructions**.
This is a current-state measurement, not a new win. Goal unchanged, 1.05x.
Both originals also verify size 100 with the same hash
`23674002 573a4409d3d2efb9a2072aff97c6cefa6111b1b90cc7a0ef`.

**Where the cost is:** a separate short cycle profile per original, 13,372
and 12,332 samples respectively without losses, gives VdbeExec 17.82 % for
VIR and 18.85 % for clang. IndexMoveto 8.77/9.08 %, RecordCompareInt
4.99/5.15 %.  The shares are not per-function speed ratios: inlining moves
work between symbols, and a single profile has sampling noise.  The VM is a
big target but the profile does not prove that it alone explains the time
gap.  Individual instruction samples are not used as latency evidence (skid
after branches).

**Dynamic mix, only instructions in VdbeExec:** one extra nop per selected
assembly instruction, the difference in instructions:u against the respective
unprobed assembly control, main size 100. Each variant is verified on size
10. Checks before/after differ by 54 and 126 instructions respectively. An
important methodological error caught: clang's internal loop alignment
changes the amount of nop padding executed when probes are inserted. The
final table therefore removes internal align directives in both control and
probes (`mix-clean/`); the original binaries above are used for the
time/cycle profile. The classes are disjoint and include xor of a register
with itself as a constant, not arithmetic.  The sum against the total probe
deviates by at most 1.8 M (0.03 %); the numbers are rounded to M.  These are
work counters, not timing measurements of instrumented code.

| Instructions executed in the VM, millions | clang | VIR | Excess |
|---|---:|---:|---:|
| Total | 5646 | 7077 | +1431 |
| Plain mov loads | 1107 | 1396 | +289 |
| Register copies | 348 | 594 | +246 |
| Constant materialization, including xor zeroing | 91 | 319 | +228 |
| Address computation + other ALU | 1105 | 1347 | +242 |
| Rsp loads + rsp stores | 849 | 988 | +139 |
| Register extensions | 80 | 193 | +113 |
| Plain mov stores | 238 | 314 | +76 |
| cmp/test | 692 | 745 | +53 |
| Conditional jumps | 668 | 709 | +41 |
| Unconditional jumps | 361 | 336 | −24 |
| Calls | 74 | 73 | −1 |

Memory operands in ALU/cmp are in their instructions' classes, not in the mov
loads; the table is not all memory accesses or micro-operations.  LEA alone
is +561 M but other ALU is −319 M: count them together before address
folding is prioritized.  The VM's instruction gap is about a third of the
whole program's, not the whole gap.

**Concrete hot paths and the next delimitation:** the jump table connects
opcode 94 to `.LBB6266_1443` (Column), 39 to `_2119` (Next), 97 to `_1286`
(MakeRecord), 81 to `_1693` (SCopy).  Extra nops at the entries count
22.592 M Column, 12.415 M Next, 2.006 M MakeRecord, 0.990 M SCopy against
83.351 M dispatch laps.  Column's body, the emitted block range 1443-1589,
accounts for about **3.090 G instructions**, of which **299 M register
copies** and **62 M mov of zeros**.  It is a block range, not including the
instructions of called functions or an exact time share per opcode; layout
copies outside the range are not included.  Hot blocks in the cycle profile
also point to Column: cursor/cacheStatus, header decoding and destination.

- First larger lead: Column's load/copy chains and register pressure across
  header decoding, SerialGet and memcpy.  Count allocator edits on exactly
  these paths and compare clang's corresponding work before a general
  heuristic.
- Smaller, clearly delimited lead: **165 M** `mov $0` near the jump back to
  dispatch (often both rcx and rdx), of **245 M** such zeroings in the whole
  VM.  VIR's printed loop edge forwards block arguments; the extra physical
  zeros need to be traced to the allocator's entry/edge plan before they can
  be removed.  `alloc.c` inherits live-in registers from a chosen
  predecessor, and `vx_edge` materializes edge constants.  This is a
  hypothesis about where the improvement lies, not proof that the zeros are
  dead.  Even if all these 165 M disappeared, it is under 0.8 % of the whole
  program's instructions: alone it is not enough for the time goal.
- The slot for aMem (776(%rsp)) is loaded about 92 M times.  This motivates
  a review of which long-lived value gets a callee-saved register; reserving
  a register may at the same time increase other spills.
- Lower priority here: more general branch/tail-call peepholes or small
  helper functions without new dynamic evidence.  The VM already runs fewer
  unconditional jumps and about as many calls as clang.

Basis: `work/vir-vm-audit/{timing/summary.json,vir.perf,clang.perf,
vir-annotate.txt,clang-annotate.txt,vm.vir,focus.json,mix-clean/mix.json}`.
`mix.py` and `focus.py` in the same directory save the probes and raw perf
counters; `mix-clean/mix.py` is the corrected class measurement.  Vpp SHA256
`ff40d17baee9aec0b8ca7f60fe8e3111c10986ae2b98eeb19aac6c267f8fbbec`.
No runtime win is claimed, no changes to the other session's source code.


2026-11-13, CI fix for the dispatch test's ARM line table:
PR130's ARM job found that vir_x86_dispatch.c gave different objects from
`-g -c` and clang's assembler on `-g -S`, despite identical machine code.
The row for atomic_thread_fence landed four bytes late.  The cause was the
assembler's `.inst`, which did not consume a pending `.loc` before the
instruction.  It now does, in the same way as an ordinary mnemonic.  A new
direct assembler test dwarf-inst.s covers single and comma-separated raw
instructions between line breaks; the old build fails the object comparison.
Validated: full DWARF rig 1591/1591 (436 skipped), assembler objects 62/62
byte-identical with clang, also through ASan/UBSan and a file-by-file build.
The SQLite binary is byte-identical to the latest performance build: no new
speed win or reached 1.05x gate is claimed by the debug fix.

2026-11-13, rejected sign ranges and local dead-store cleanup:
The next clang comparison concerned the division `(serial_type-12)/2` in
vdbeRecordCompareString.  A preceding branch proves that the value is at
least 12, so the rounding bias is not needed.  A shared prototype used
signed constant comparisons on dominating single-entry branches, plus simple
zero-extension/mask facts, and removed four x86 instructions there.  Focused
x86 tests and SQLite hashes matched, but two alternating five-sample series
on CPU 7 gave 1221.13 → 1234.00 ms and 1226.22 → 1243.30 ms (clang 1116.06
and 1115.32 respectively).  Instructions/clang 1.263x → 1.261x, CPU 1.1-1.4 %
worse.  No such range analysis is kept.

A second prototype cleaned earlier non-volatile stores that a later store to
exactly the same bytes replaced within the block, without an observing
read/call between them.  Eight candidates per block; separate fields could
be proven disjoint through the existing base/offset analysis.  Doing this in
the same sweep as load forwarding gave only 22 fewer static instructions and
no CPU win (1220.89 → 1223.59 ms, clang 1111.29).  Moved after load
forwarding, the pass avoided reads that were already gone by then, but still
gave only 384111 → 384042 instructions in the whole amalgamation; VdbeExec
17291 → 17293.  CPU 1220.31 → 1228.34 ms, clang 1114.72, instructions/clang
still around 1.263x.  Focused tests and hash green, but the prototype is
reverted.  An intermediate test of disjoint fields had byte-identical
assembly to the first version; its timing overlapped a build and is not used
for conclusions.

A short trace of equally sized integer/FP stores that block slot promotion
found only kahanBabuskaNeumaierStepInt64, sumStep and sumInverse in SQLite.
No union optimization was introduced from that basis.  All trace changes are
reverted.  Basis `work/vir-x86-perf/signrange/{results100,repeat100}`,
`work/vir-x86-perf/local-dse/results100`,
`work/vir-x86-perf/local-dse-late/results100` and
`work/vir-x86-perf/{mixed-trace,copy-trace}`.  **The 1.05x goal is still
open; the latest kept improvement is the slot promotion below.**

2026-11-13, promote narrow local fields despite different integer carriers:
The comparison of `vdbeRecordCompareString` with clang's assembly showed
that serial_type stayed on the stack.  The first promotion is blocked by an
inlined pointer parameter.  After simplification the address is free, but a
folded-away trunc before a four-byte store gives an i64 carrier next to other
i32 stores.  The field model therefore rejected the same memory field as
having different store types.

The shared builder now normalizes integer fields of at most four bytes to
i32 before store types are compared, and puts a trunc on the i64 value when
the field is promoted.  A four-byte load to i64 extends the field's i32
value.  Access widths, volatile, escape and overlap rules are not changed.
ARM and x86 use the same code.  The string comparison's serial_type now goes
in a register; its code shrinks 554 → 535 bytes.  VdbeExec is only
marginally affected; the win does not come from reducing inlining in
general.

Two alternating five-sample series, CPU 7, speedtest1 main size 100:

| Series | clang ms | Before ms | After ms | After/clang |
|---|---:|---:|---:|---:|
| First | 1110.71 | 1226.01 | 1214.68 | 1.094x |
| Repeat | 1109.10 | 1227.85 | 1215.77 | 1.096x |

CPU win 0.92-0.98 %, cycles improve too.  Dynamic instructions
21.204 → 21.065 billion, the ratio against clang 1.272x → 1.263x.
All verification hashes match, and the final build's SQLite binary is
byte-identical to the candidate measured first.  **The 1.05x goal is open.**
Basis `work/vir-x86-perf/promote-width/{results100,repeat100}` and
`work/vir-x86-perf/promote-width-final`.

`vir_slot_width.c` tests 8/16/32-bit fields, signed and unsigned returns to
64 bits, different store paths, high input bits and loop phi.  The focus rig
requires stack-free mixing functions: the old build kept stack stores and
misses the check.  Validated: own x86 318/318, GCC 1615/1615, TCC 105/105
and the same five named inline failures; ARM/QEMU 49/49 VIR tests,
ASan/UBSan and file-by-file build green.  TSVC 151/151 for direct and VIR,
**83** vector loops and 158 lowered functions; prefix check 134.  ARM's
native performance is not measured.

2026-11-13, rejected attempt to also limit the constant-argument bonus:
The same limit on the caller's own AST was tried for the 600-node bonus in
both prepass and walker.  The VM shrank a further ca. 11 % and the frame
1800 → 1672 bytes, but speedtest1 main size 100 became slower:
1252.25 → 1262.29 ms in the first alternating five-sample series and
1225.28 → 1242.53 ms in the repeat (clang 1130.84 and 1113.10 respectively).
Instructions/clang 1.272x → 1.273x; all verification hashes matched.
The attempt is reverted: the context-sensitive budget is kept in large
callers too.  Basis `work/vir-x86-perf/large-context/{results100,repeat100}`.
A new short profile of the production build still points to VdbeExec
(19.3 %), IndexMoveto (8.6 %) and RecordCompareInt (4.9 %); the profile is
about 2000 cycle samples and is used for direction, not exact function wins.

2026-11-13, limit the one-time bonus in callers that are already large:
The common walker and the bottom-up size model no longer give the 4000-node
bonus to a one-time body when the caller's own AST is already larger than
4000 nodes.  The ordinary 150-node budget and the context-sensitive 600-node
budget remain; always_inline, noinline and the other legality rules are not
changed.  The decision uses the original caller at each nested call site,
exactly as the call graph's size model.  The same code is used for ARM and
x86; no function is identified by name.

The measurement pointed to the VM: address-matched swaps of clang's
IndexMoveto and TableMoveto gave no whole-program win (1244.93 ms control,
1251.77 index, 1243.61 table, 1247.02 both; clang 1112.85).  A VM hybrid
first required printing static helper functions that are normally inlined
away, without changed inlining decisions; the VM's assembly was
byte-identical to the original.  Clang's VM then called these VCC helpers.
Shared static objects were mapped to VCC's originals and private constant
objects were kept in the VM's text slot.  All 2526 text addresses matched
between control and hybrid.  It gave 1263.74 → 1216.81 ms in the first
series, but only 1365.33 → 1351.09 under load in the repeat.  These are
diagnostic hybrids, not a compiler win.  Basis
`work/vir-x86-perf/hybrid-large/{index,roots,vm}`; the temporary change that
printed all static functions is reverted.

The actual budget change gives in SQLite VdbeExec 24854 → 17295
instructions (−30.4 %), 119382 → 82806 code bytes and 2680 → 1800 bytes of
stack frame.  The whole amalgamation 387383 → 383946 static instructions.
Dynamically in speedtest1 main size 100: 21.908 → 21.204 billion
instructions, the ratio against clang 1.314x → 1.272x.

Alternating CPU medians in ms, same sources/flags and verification hash:

| Series | CPU | Samples | clang | Before | After | After/clang |
|---|---:|---:|---:|---:|---:|---:|
| First | 7 | 5 | 1133.16 | 1266.92 | 1245.37 | 1.099x |
| Contended repeat | 7 | 5 | 1244.04 | 1329.83 | 1408.20 | 1.132x |
| Contended other core pair | 4 | 5 | 1381.46 | 1675.55 | 1558.73 | 1.128x |
| Calmer after the workers stopped | 7 | 5 | 1113.51 | 1241.84 | 1228.60 | 1.103x |
| Final build, short confirmation | 7 | 3 | 1127.85 | 1261.80 | 1244.99 | 1.104x |

Six other Python workers drove the CPU times up strongly in the two
contended series; the raw samples are saved without filtering.  The three
calmer series give a **1.1-1.7 % win**, but **the 1.05x goal is still
open**.  The final build's SQLite binary is byte-identical to the candidate
measured first.  Basis
`work/vir-x86-perf/large-once/{results100,repeat100,idle4,quiet7,final3}` and
`work/vir-x86-perf/large-once-final`.

`vir_inline_large.c` keeps a real call boundary to a one-time helper in a
large arithmetic function; the results are compared with separate loops over
the same unsigned computation, also for extreme input values.  Existing
tests keep the one-time bonus for small callers.  Validated: own x86 317/317,
GCC 1615/1615, TCC 105/105, the same five known inline failures (names
checked), focused ASan/UBSan and file-by-file build green.  ARM/QEMU 48/48
VIR tests; ARM's native performance is not measured.  TSVC 151/151 checksums
and **83** vector loops remain, 158 lowered functions.  Prefix check 134
names and the focus rig's existing O0/O1 tests green.

2026-11-13, common context-sensitive inlining:
VIR's shared prepass/walker now saves the call site in the call graph.  A
constant integer or null pointer to a referenced parameter can give a budget
of up to 600 AST nodes, against the ordinary 150.  References that are only
`(void)param` give no bonus: SQLite's unused BtreeNext flags should not
cause larger copies.  This is a profitability heuristic; the existing
folding of the inlined body does the specialization.  O0/O1 are not changed.
ARM and x86 use the same rule, without new CPU-specific optimization code.

The bottom-up size also counts the existing 4000-node bonus of one-time
calls.  Before, a small wrapper with a large inlined one-time body could
itself be counted as small and copied further.  Saturated sums prevent
wraparound; SCC, noinline, VLA and target limits remain.  The regression
covers this wrapper, constant/unknown arguments, a real parameter reference
after `(void)param`, and a fully ignored parameter.

Five alternating speedtest1 main samples, size 100, CPU 7; the same chosen
SQLite binary in three series (the final build is byte-identical):

| Series | clang CPU ms | Before CPU ms | After CPU ms | After/clang |
|---|---:|---:|---:|---:|
| A | 1111.25 | 1244.90 | 1238.60 | 1.115x |
| B | 1107.02 | 1247.98 | 1233.83 | 1.115x |
| C | 1114.08 | 1248.77 | 1240.06 | 1.113x |

Win 0.5-1.1 %, but **the 1.05x goal is still open**.  Dynamic
instructions/clang 1.320x → 1.314x.  VdbeExec 26743 → 24854 static
instructions and frame 2888 → 2680 bytes; the whole amalgamation grows
369505 → 387383 instructions.  The corrected one-time size alone was
0.5-0.9 % slower.  A 450-node bonus was neutral (1248.77 → 1247.72), 750
was slower (1247.98 → 1254.03); a bonus only inside loops was also slower
(1248.72 → 1261.31).  These variants are removed.  Basis
`work/vir-x86-perf/inline-{cost,nested,realused3,realused4,realused5,loop4,final}`.
All validate the same speedtest1 checksum.

Validated: own x86 316/316, GCC 1615/1615, TCC 105/105 and the same five
known inline failures; focused ASan/UBSan and file-by-file build green,
O0/O1 unchanged.  ARM/QEMU 47/47 VIR tests.  TSVC 151/151 checksums,
**83** vector loops remain; the walk-prefix check 134 names.

2026-11-13, measure isolated whole-program win before the next optimization:
`bench/sqlite/vir-x86-swap.py` builds diagnostic hybrids with clang's
assembly for one small function at a time and for all chosen together.
There is also a VIR control with the same 2048-byte text slots.  Local
labels are renamed; jump tables are kept in text.  The tool requires the
addresses of all 1478 text symbols to be identical between the control and
each hybrid, and rejects external local string/data references.  No compiler
or runtime code in the project is changed by this measurement.

Tested vdbeRecordCompareInt, btreeParseCellPtr, getCellInfo and
pcache1Fetch, same -O2 code and ABI as the reference.  All six binaries pass
the speedtest1 verification with the same hash.  Five alternating main
samples, size 100, CPU 7, median CPU ms:

| Binary | Ordinary function sizes | Address-matched text slots |
|---|---:|---:|
| VIR control | 1233.85 | 1242.85 |
| Clang RecordCompareInt | 1238.13 | 1247.51 |
| Clang btreeParseCellPtr | 1232.87 | 1236.11 |
| Clang getCellInfo | 1232.70 | 1244.09 |
| Clang pcache1Fetch | 1235.76 | 1257.43 |
| All four from clang | 1236.95 | 1237.05 |
| The whole clang reference | 1113.70 | 1111.39 |

Swapping all four reduces dynamic instructions 21.908 → 21.500 billion, but
the first address-matched series wins only 0.5 % in time.  A repeat gave
control 1257.15, cell 1240.47, all four 1231.27 and clang 1122.63 ms.  That
series has a visible load/frequency change from the third round (the raw
samples are saved without filtering), so its 2.1 % should not be read as a
stable win.  A short third series (three alternating samples) gave
1242.27 → 1232.74 ms for all four, clang 1109.42 ms: 0.8 % win, 1.111x
clang.  The more stable series thus point to about 0.5-0.8 %.  Even these
hybrids are clearly above 1.05x clang.  This does not rule out other
improvements, but motivates that the next effort focuses on the VM and the
larger search routines rather than on
chasing clang's instruction count in exactly these four small functions.

Build the matched directory with `vir-x86-swap.py --vir VIR.s --clang CLANG.s
--driver-object vir-speedtest1.o --out DIR vdbeRecordCompareInt
btreeParseCellPtr getCellInfo pcache1Fetch`; the timing is done by the
existing `vir-x86.py run`.  Basis
`work/vir-x86-perf/hybrid-small/{results100,matched/{results100,repeat100,confirm100}}`.

2026-11-13, common edge-sensitive branch threading tried:
A shared layout prototype could skip a pure icmp+brif when incoming block
arguments were constant or an earlier edge had already proven the same/
inverted integer condition.  It preserved incoming edge copies, rejected
edits, external comparison uses and parameter/comparison spills, and
required empty outgoing edge copies for directly retargeted label targets.
A bounded backward step through unique predecessors stopped at operand
definitions, so facts did not survive a new loop iteration.  Both backends
used the same proof; ARM's prologue/save-point rules got a separate guard
check.

The first focus run caught a bug in the reading of BR's uncounted argument
list (the list has no leading count).  It was fixed and a targeted self-test
was added.  The final focus runs and self-tests passed.  An early prototype
threaded only the fallthrough path; the final one also moved taken condition
edges.  Then pcache1Fetch's key hit goes directly past the second null test,
and BtreeNext loses its first constant rc test.  A general ARM/sanitizer
gate has not been run on the prototype.

Still no whole-program win on speedtest1 main size 100: constant edges only
1231.83 → 1240.59 ms; direct repeated/inverted condition 1240.80 → 1244.72
ms (repeat 1234.60 → 1255.43); extended predecessor facts 1232.48 → 1243.62.
Final both-edges version 1234.81 → 1242.08 ms, repeat 1237.36 → 1245.38 ms
(about 0.6 % slower in both).  It reduces 387383 → 386208 static
instructions and the dynamic instruction ratio/clang 1.314x → 1.311x.  All
measurement series have five alternating samples and the same verification
hash.  **No prototype is kept.**  Basis `work/vir-x86-perf/thread{,2,3,4,5}`
and `/tmp/vx-layout-thread-experiment.patch` (including the self-tests).

2026-11-13, more short profile-based x86 attempts:
A fresh size-100 profile (about 2000 cycle samples, no losses) has VdbeExec
20.35 %, IndexMoveto 7.69 %, RecordCompareInt 4.21 %, TableMoveto 3.90 %,
btreeParseCellPtr 3.18 %, pcache1Fetch 2.92 %.  Short assembly comparisons
against clang gave the following rejected prototypes:

- Unsigned big-endian halfword read with bswapl+shrl16 instead of rorw8 (61
  places): speedtest1 1233.32 → 1248.09 ms.  The rotation is kept.
- Promoted bytes with a proven range 0..255 can be compared with 128/127 via
  testb and S/NS instead of cmp.  All eight relevant signed/unsigned
  predicates, 16-bit counterexamples and branch tests passed over the whole
  byte/halfword range in the focus rig; assembly and own object path also
  green.  Whole speedtest1 1285.07 → 1288.19 ms, no win.
- Indirect calls directly in the allocated target register; the copy to r11
  is needed only when the variadic AL write destroys a target in rax.
  Removes 990 static instructions (eight in IndexMoveto), 21.908 → 21.831
  billion dynamic.  New variadic function-pointer and integer stack-argument
  tests passed the focus rig.  Whole speedtest1 1236.05 → 1255.05 ms, repeat
  1233.44 → 1250.15 ms; **1.4-1.5 % slower**, therefore removed.

All whole-program tests are alternating main size 100 on CPU 7 and validate
the same checksum.  For the first two, five samples are used, for the call
copy's repeat three.  Basis `work/vir-x86-perf/{halfword,bytethreshold,callreg}`;
the prototypes are saved only as patches in /tmp, no such code is kept.

2026-11-13, physical extension forwarding and function alignment tried:
An x86 prototype let the next ALU consumer of a private, physically
identical i32→i64 extension read the source register directly.  No
intervening edit, external use or spill slot was allowed.  It removed seven
copies in btreeParseCellPtr and 32 instructions in the whole of SQLite; the
focus rig with own object path passed.  A 64-byte-aligned two-byte varint
test with 30 million calls, seven alternating rounds, went 50.160 → 48.556
ms (−3.2 %), clang 41.434 ms, identical checksum 124034992832.

Whole speedtest1 main size 100: 1231.57 → 1241.74 ms, repeat 1248.40 →
1247.35 ms.  **No safe whole-program win**, therefore removed.  Separate
assembly variants with function alignment 32/64 bytes in the whole
amalgamation gave no win either: in the same five-sample series base
1235.29, 32 bytes 1244.92, 64 bytes 1244.46; forwarding plus 32/64 bytes
1246.78 / 1246.25 ms.  The ordinary 16 bytes is kept.  The tests do not
state the cause of the time variation from the small code changes.  Basis
`work/vir-x86-perf/{extforward,function-align}` and
`/tmp/vx-extforward-experiment.patch`.

2026-11-13, rejected narrow relation test in memory:
Extended x86's private load+icmp folding to bytes/halfwords with a constant
within the source type's range.  Signed comparisons of zero-extended values
switched to the corresponding unsigned condition; signed sources under
unsigned comparison were also tried.  Exhaustive byte/halfword tests,
assembly and own object path passed.  RecordCompareInt loses movzwl and gets
`cmpw $1,28(%rdx); jbe`, but fewer instructions do not help here.

Two five-sample series of speedtest1 main size 100: 1233.72 → 1247.42 ms and
1232.42 → 1245.94 ms, about **1.1 % slower**.  Static instructions 387383 →
387172 and dynamic 21.908 → 21.856 billion.  A separate equal-key test of
RecordCompareInt (30 million calls, seven alternating rounds) goes 39.830 →
45.620 ms; the repeat 40.192 → 45.459.  A handwritten `cmpw $2; jb` variant
gives 45.219 ms, i.e. the same regression.  Clang 33.853 ms in the repeat.
The microtest keeps the compiler's other function code and uses an identical
64-byte-aligned start and checksum; it does not isolate the cause of the
regression.  **Nothing of the attempt is kept.**  Basis
`work/vir-x86-perf/cmpnarrow`, patch `/tmp/vx-cmpnarrow-experiment.patch`.

2026-11-13, already extended indices in small hot functions:
x86 reuses the same physical sign-extension proof in plain ext and NSW-scaled
addresses.  An unsigned 8/16-bit load already has the right high bits; a
signed load/extension can be reused if the value has no spill slot.  An i32
spill is reloaded with a zeroed upper half and therefore still gets movslq
for a negative index.  Nothing changes in SSA or in the allocator's
lifetimes.  In getCellInfo, movslq disappears after the iCur read.

A dependency-chain test (30 million calls, seven alternating samples) of a
halfword-indexed read goes 81.389 → 75.971 ms (−6.7 %), identical checksum.
Whole speedtest1: 1301.57 → 1297.17 ms in the first five-sample series,
1248.86 → 1248.94 ms in the repeat; thus **no safe whole-program win**.  The
second series is 1.125x clang, the instruction ratio 1.321x → 1.320x.  The
1.05x goal is still open.  Basis `work/vir-x86-perf/index-ext`, including
`latency-results.json`, `results100` and `repeat100`.

The regression reads unsigned short and signed char indices and covers the
whole byte range.  A pressure test keeps 13 signed indices across a call
that changes the source memory: five unspilled indices need no extra
extension, eight i32 spills must still get it.  Ordinary and -fwrapv runs,
assembly and own object path check the same result.
Validated: own x86 315/315, GCC 1615/1615, TCC 105/105 and the same five
known inline failures; focused ASan/UBSan and file-by-file build green.
ARM/QEMU 46/46 VIR tests.  TSVC 151/151 checksums, **83** vector loops
remain.

Fresh short profile of the same main test: VdbeExec 20.3 %, IndexMoveto
8.1 %, RecordCompareInt 4.7 %, pcache1Fetch 3.1 %, SerialGet 1.8 %.  The
following attempts are saved only as measurement basis, not as changes in
the compiler:

| Attempt | CPU ms before → after | Conclusion |
|---|---:|---|
| Absolute pointer table in data.rel.ro, direct memory jmp | 1266.43 → 1264.33; 1268.81 → 1277.13 | 1.4 % fewer instructions but no stable win |
| SIMD scratch around GP copy cycles on edges | 1274.86 → 1285.90 | A freer r10 in more functions does not help; the VM code was already as large |
| Renew GVN's latest load candidate after a write | 1254.17 → 1270.29 | Fewer loads, but slower |
| GVN: whole stored values across non-overlapping writes | 1251.36 → 1270.13 | Slower |
| GVN: also narrow stores | 1249.35 → 1249.16 | Neutral |
| GVN: narrow values only, across blocks | 1248.02 → 1256.05 | Slower |
| GVN: narrow values within blocks | 1246.47 → 1254.17 | Slower |
| GVN: within blocks plus ext i64 → i32 from low byte/halfword | 1247.62 → 1244.90 | Too small an uncertain win to keep the pass |
| Only the missing ext case in store forwarding | 1244.65 → 1257.56 | Changes only btreeBeginTrans and gives more instructions there |
| 32 instead of 8 candidates for spill slot reuse | 1253.18 → 1252.58 | Neutral; VM frame still 2888 bytes |
| Reuse also after the whole phi web's conservative lifetime | 1249.20 → 1252.38 | VM frame 2888 → 2136 bytes, but no CPU win; 30 random units green |
| Reverse physical spill order near rsp | 1249.27 → 1250.46 | The VM's short rsp displacements 80 → 1464 of 5550 references, still neutral |
| Truly frameless direct/indirect sibling calls, all calls tail | 1248.65 → 1250.65; 1249.88 → 1250.32 | 1.321x → 1.310x instructions/clang, neutral CPU |

The GVN attempts used the existing base+offset and write-range proof; the
test with narrow integer stores requires correct extension even when an i64
carrier is read back as i32.  The ext operation itself allows this per SPEC,
but the isolated rule does not improve today's SQLite.  The sibling-call
prototype shared ARM's return/br proof with x86 and also took indirect
callback calls.  It zeroed the builder's `e->calls` before lowering so that
only real call instructions require alignment frame.  Without this, older
prototypes could keep an unnecessary 8-byte frame.  Although getCellInfo,
pcache1Fetch and RecordCompareInt actually lose sub/add rsp and get jmp
instead of call, the whole-program time is neutral in two series.  Zeroing
e->calls alone leaves the SQLite amalgamation's assembly equal.

The register copy review found no vector width bug: X_MOV between XMM
registers already writes movaps independent of the record's scalar size.
No such claimed bug fix is kept.  Measurement directories under
`work/vir-x86-perf/{abstable,edge-simd-safe,gvn-fresh,gvn-store,gvn-narrow-store,gvn-narrow-only,gvn-local-store,gvn-local-ext,store-ext,slot32,webslot,spill-reverse,tail-leaf,real-calls}`.

2026-11-13, common proof for signed index scaling: the shared walker lets
VIR keep NSW when a signed multiplication by a positive power of two becomes
a shift.  The builder produces the same operation and code as before, but
the overflow fact survives.  -fwrapv and raw C shifts do not get NSW here.
Multiplication by the negative sign-bit power stays a mul.

`vir_sext_shift` in `vir/facts.c` proves that `sext(shl.nsw.i32 x,k)` can
use `sext(x)` and a later scaling.  Both backends use the proof with their
own limits for instructions, registers, one-time values and edits: x86 can
address an exact big-endian read with scale 2/4/8; ARM chooses SXTW in
ADD/SUB or load/store.  No new SSA lifetimes are introduced.  In
BtreeIndexMoveto, the shll before movslq and a read with scale 2 disappear.

Five alternating speedtest1 main samples per series, size 100 on CPU 7:

| Series | clang CPU ms | Before CPU ms | Scaled signed CPU ms | After/clang |
|---|---:|---:|---:|---:|
| First | 1113.14 | 1256.27 | 1246.51 | 1.120x |
| Repeat | 1110.58 | 1256.26 | 1250.33 | 1.126x |

CPU win about 0.5-0.8 %, also fewer cycles.  Instructions/clang 1.329x →
1.322x.  The final version with the common proof has byte-identical x86
assembly and executable with the measured local prototype.  **The 1.05x goal
is open.**  Basis `work/vir-x86-perf/nsw-scale/{results100,repeat100}` and
`work/vir-x86-perf/scale-proof`.

An alternative that rewrote SSA to a widened shift before allocation was
tried but is not kept: 1268.39 ms against 1247.02 ms for the lowering
variant (instructions 1.321x and 1.322x clang respectively).  It changed
allocation and gave e.g. balance 89 spills instead of 87.  The proof itself
is shared instead; the lowerings leave the allocator's lifetimes intact.
Basis in `work/vir-x86-perf/shared-scale`.

Validated: own x86 315/315, GCC 1615/1615, TCC 105/105, the same five known
inline failures.  A new focus test covers negative and extreme indices,
powers of two, shared expressions, volatile, signed sign-bit constants and
overflow with -fwrapv.  ARM's 46 VIR run tests pass; TSVC on ARM/QEMU gives
151/151 the same checksums as clang -O3 with -ffp-contract=off.  The proof's
unit test tries flags, unsigned ext, shift limit and missing successor.
ASan/UBSan and file-by-file build pass, also the -fwrapv variant.  VIR
format 130/130, allocator 20/20 and the prefix generator's 134 names green.
TSVC on x86 passes 151/151 and increases from 82 to **83** real vector loops,
all in lowered functions (158 in total): s1111, the write to a[2*i], becomes
linear when the overflow fact is kept.  A local intstride test also requires
SIMD for int indices and tests the tail, untouched intermediate elements and
overlapping buffers that must take the scalar path.

2026-11-13, rebase and scaled integer addresses: the branch is now on
`5a111ce5` (PR127-129).  ARM's new copy of a block with an ordinary
operation and `br` is part of the common layout rule, also for x86.  A new
unit test for size/depth/cold/address-taking/calls and the existing
`vir_break_dup.c` in the x86 focus rig; ARM's break/rotation/join tests match
the reference under QEMU.  Plans/specifications follow main to `doc/`.

A proposed change to i32 carriers for 32-bit ABI parts is not kept.  Only
parameters: 1270.81 → 1270.54 ms.  Also call arguments/results and return
values: 1271.91 → 1271.33 ms, instructions/clang 1.344x → 1.342x.  The win
does not justify changing SPEC's decision O1.  Format and builder are
restored.  Basis `work/vir-x86-perf/{param32,abi32}`.

The kept x86 change removes the scaling MUL/SHL also when the address is
`add.i64` + `i2p` in front of the memory access.  The same requirements on
one-time values, spills and edits as for `ptradd`.  `pcache1Fetch` becomes,
for example, `movq (%r8,%rdx,8), %rax` instead of `imulq $8, %rdx, %rdx` and
a load with scale 1, as the comparison with clang pointed out.

Five alternating samples per series, speedtest1 main size 100 on CPU 7:

| Series | clang CPU ms | Before CPU ms | Scaled address CPU ms | Scaled/clang |
|---|---:|---:|---:|---:|
| First | 1111.14 | 1281.37 | 1249.30 | 1.124x |
| Repeat | 1112.07 | 1279.26 | 1250.42 | 1.124x |

About 2.3-2.5 % lower CPU time, also fewer cycles; instructions/clang
1.336x → 1.329x.  Both have the same verification hash.  **The 1.05x goal is
open.**  Basis `work/vir-x86-perf/castscale/{results100,repeat100}`.  A new
run test covers scales 2/4/8, negative indices, modulo indices, a volatile
store, an index that is also stored and a scale 3 that must not become a
hardware scale.  Validated: own x86 314/314, GCC 1615/1615, TCC 105/105,
the same five known inline failures.  Focus tests with own assembler,
one/six threads, ASan/UBSan and file-by-file build green.  VIR 130/130 and
builder self-test green.  TSVC 151/151 checksums, 82 vector loops and 158
functions through VIR.

2026-11-13, comparison with a memory operand: x86 can now fold an adjacent,
ordinary one-time load directly into `cmp`.  The same width is used as the
load; narrow loads are allowed only for ==/!= against a representable
constant.  Volatile, shared results and intervening allocator edits are left
as they are.  `X_CMPM` describes base, index, offset, comparison direction
and register/imm.

Speedtest1 size 100, five alternating samples per series:

| Series | Before CPU ms | Memory cmp CPU ms | Memory cmp/clang |
|---|---:|---:|---:|
| First | 1279.08 | 1270.01 | 1.145x |
| Repeat | 1288.04 | 1293.41 | 1.154x |
| Third | 1277.17 | 1273.47 | 1.141x |

Instructions/clang 1.358x → 1.344x; the CPU win is small and varies.  The
1.05x goal is still open.  Basis `work/vir-x86-perf/cmpmem` and
`work/vir-x86-perf/lastmem/repeat100`.  Direct spill operands, limited
last-use spill operands and forwarded LEA addresses were tried but are not
kept: they gave no further safe CPU win compared with memory cmp.  ADD
instead of LEA when the result already shares a register with an operand was
also rejected: 1275.98 → 1286.10 ms, the same instruction count.  Patches
and runnable variants saved outside the checkout.

Validated: own x86 308/308, GCC 1615/1615, TCC 105/105, the same five known
inline failures.  A focused run test tests both comparison directions,
indexed address, 8/16/32/64 bit, negative constants, promotion limits,
volatile and multiply-used loads, both assembly and own object path.
ASan/UBSan and file-by-file build green; TSVC 151/151, 82 vector loops and
158 lowered functions.

A new quick dynamic instruction count on the main test, size 30, puts a NOP
before each instruction in a class and takes the difference against the
base.  Clang 4.228 G, vcc 5.762 G instructions.  Register copies 0.272 →
0.620 G; extensions 0.421 → 0.631 G.  LEA+integer ALU together 0.784 →
1.038 G.  Classes overlap (e.g. a signed stack load is also an extension).
Push/pop must be counted in the stack comparison: clang has 0.122/0.109 G,
vcc 0.001/0.001 G.  Basis and run scripts in
`work/vir-x86-perf/dynamic-mix/{results.txt,extra-results.txt,run.py,extra.py}`.

2026-11-13, private switch selector directly in the dispatch register:
x86's dense table and allocation requirements now use the same form
computation.  A selector with exactly one VIR use can be produced directly
in r10; the consumption is a late clobber.  Sparse interval chains need the
original several times and keep their early clobber.  An immediately
preceding unsigned 32-bit definition needs no new movl to zero the upper
half.  In vdbeRecordCompareInt, the movl after movzbl of the serial type
disappears.

The common allocator releases a dead operand/fixed copy **only in the
registers the terminator clobbers**, before the preservation of live values.
A new @dead test goes from one unnecessary copy to zero; @keep and @loop keep
live selectors.  Running the whole phase 3 for all terminators was rejected:
random seed 17 gave check failures that the base does not have.  The narrow
rule passes seeds 1-30 (240 functions), including this seed.  The SQLite
binary is exactly identical to the earlier local, wider prototype's: its two
five-sample series 1293.87 → 1279.28 and 1286.27 → 1280.22 ms also apply to
the final version.  About 0.5-1.1 % CPU win, 1.131x/1.118x clang,
instructions 1.322x → 1.321x; small layout-sensitive wins, not reaching the
1.05x goal.  Basis `work/vir-x86-perf/{switch-sole,deadclob}`.
A third, more loaded five-sample series: 1370.78 → 1333.07 ms, 1.140x clang.
Absolute ratios vary with load; 1.118x is not a closed gate.
Validated after the narrow rule: own x86 315/315, GCC 1615/1615, TCC 105/105
and the same five known inline failures.  Allocator tests 20/20, 30 random
units/240 functions, VIR format tests 130/130, ARM/QEMU 46/46, ASan/UBSan
and file-by-file build green.  TSVC 151/151, **83** vector loops.

Rejected preceding attempts: a flag cache within blocks removes 102 static
instructions but is neutral in speedtest1 (1289.22 → 1288.37 ms).  Select
limit 4 or 2 changes only planning functions and gives no win;
multi-result select without speculative arm work does not improve either
(1268.87 → 1277.47 ms).  A general fixed r10 for dense switches, also shared
selectors, increases preservation copies: instructions 1.330x clang, higher
than the base's 1.322x.  Even with dead operands released and the
unnecessary movl removed it is 1.327x and neutral CPU.  These attempts are
not kept.  Basis
`work/vir-x86-perf/{cond-flags,select4,select2,select-work,switch-late,term-kill,switch-u32}`.

2026-11-13, signed 32-bit decoding in small SQLite functions:
`vir_read_be` now also accepts a signed first byte when its i32 shift is 24
bits.  The sign extension's high bits disappear in exactly that shift; the
rule does not apply to 16-bit results or signed later bytes.  The same
private uses, volatile limit and the targets' edit/spill check apply.
`sqlite3VdbeSerialGet` goes 122 → 114 machine instructions: the four-byte
case becomes movl + bswapl + movslq instead of four reads and shift/OR.
A fixed-aligned test with 30 million calls and seven rotated samples gives
31.441 → 28.363 ms CPU (−9.8 %), clang 28.225 ms, equal checksums.
Whole speedtest1 is layout-sensitive: two five-sample series give 1288.55 → 1274.63
and 1271.56 → 1269.82 ms respectively; the instruction ratio stays at 1.322x clang.
No stable whole-program win of 1 % is promised.  The goal of at most 1.05x is open.
Basis: `work/vir-x86-perf/signed-be/{decoder-results.json,results100,repeat100}`.
The regression covers all byte values/alignments, negative result, signed
later bytes and volatile; both assembly and own object path.
Validated: own x86 315/315, GCC 1615/1615, TCC 105/105 and the same five
known inline failures.  Focused ASan/UBSan, file-by-file build and ARM/QEMU
green.

Rejected follow-ups to the same small functions: signed 16-bit load + rorw +
movswq reduces vdbeRecordCompareInt by four instructions, but a fixed-aligned
30-million test of the two-byte case gives 42.029 → 52.237 ms (clang
41.465).  bswapl + sarl instead of the 16-bit rotation does not help:
42.304 → 52.462 ms.  The six-byte case improves negligibly and the
four-byte case is neutral.  Whole speedtest1 becomes about 0.9-1.0 %
slower.  Both variants are removed.  Basis
`work/vir-x86-perf/{signed16,signed16-wide}`; focused run tests passed these
attempts too, so the rejection concerns performance.

Common join copying with at most three instructions was also tried
(1310.26 → 1316.39 ms).  A narrow rule for load+icmp+brif with any constant
ptradd gives an even worse 1279.96 → 1311.55 ms despite fewer dynamic
instructions.  vdbeRecordCompareInt copies the comparison into the switch
arms, but must still jump to separate return paths: the code growth buys no
safe win.  These changes are also removed.  Basis
`work/vir-x86-perf/{join3,loadcmp-join}`.  No new target-specific policy has
been introduced and the earlier common ARM/x86 layout is kept.

2026-11-13, small SQLite functions: the structural proof of byte assembly
has been moved from x86 to `vir/facts.c` (`vir_read_be`).  It normalizes
constant pointer offsets and also recognizes two 32-bit halves as a 64-bit
big-endian read.  x86 chooses one exact wide load and bswap; no extra bytes
are read, and neither volatile, signed bytes nor externally used
intermediate values may be combined.  The edits/spill check stays in the
target.

`vdbeRecordCompareInt`: 102 → 82 machine instructions; the eight-byte case
is now movq+bswapq, the six-byte case uses movl+bswapl for the unsigned
four-byte part.  The same rule improves `sqlite3VdbeSerialGet` 138 → 122 and
`walDecodeFrame` 95 → 71.  Separate 300-million tests gave about 17 % lower
CPU time for the six-byte case and 11 % for the eight-byte case in the
linked SQLite objects.  A separate fixed-aligned function test changed the
result strongly: the wins are **layout-sensitive**, not stable promises per
data type.

Whole speedtest1, five alternating samples, size 100: 1275.02 → 1274.71 ms;
repeat 1274.46 → 1275.21 ms.  Both about 1.148x clang, so no measurable
whole-program win from the byte reading.  Basis:
`work/vir-x86-perf/byteflags/{results100,repeat100}` and
`work/vir-x86-perf/record-int` (the byteflags series also contain the kept
byte64 variant).  Flag reuse across simple branch chains and moved switch
tables were tried but are not kept: about 1 % slower SQLite, despite fewer
instructions for the flag variant.  The 1.05x goal is open.

The regression test covers 16/32/64 bit, offset 32-bit, signed 48-bit, all
byte alignments and separate volatile reads.  Own x86 307/307, GCC 1615/1615,
TCC 105/105, the same five known inline failures; focused ASan/UBSan and
file-by-file build green.  VIR format tests 130/130 and allocator tests
20/20.  TSVC 151/151 checksums, 82 vector loops and 158 functions lowered.

2026-11-13, allocation at switch: the common allocator now moves live values
out of late/early clobbers also before conditional terminators' outgoing
state.  Before, the phase was skipped.  x86 can therefore use r10 between
all switches and declare it as an early clobber at dispatch.  Two
five-sample series of speedtest1 size 100: 1276.10 → 1268.37 ms and
1273.99 → 1268.01 ms; the latest then 1.141x clang.  Instructions/clang
1.364x → 1.358x.  A synthetic switch/loop test fails with the old allocator
and passes with the fix, also over 48 register configurations per function
and with linear fallback.  Allocator tests 20/20 and 30 randomly generated
units green.



**2026-11-13: ARM comparison and reconsidered x86 attempts.** The current
user goal is at most **1.05x clang -O2** for speedtest1; it is still open.
The same SQLite 3.50.4 source, `-O2`, `SQLITE_THREADSAFE=0` and
`SQLITE_OMIT_LOAD_EXTENSION` have been lowered with `VCC_VIR=bvls` and
allocator check for both targets.  ARM uses its Linux cross sysroot.  Static
instructions are not a comparison of cycles between different processors:

| Function | ARM64 instructions | x86-64 instructions | ARM64/x86 spills |
|---|---:|---:|---:|
| sqlite3VdbeExec | 17083 | 23197 | 379/570 |
| sqlite3BtreeIndexMoveto | 537 | 627 | 0/9 |
| vdbeRecordCompareInt | 81 | 102 | 0/0 |

Concrete ARM advantages: bit/zero branches without separate flag
materialization, extended index addition and shifted OR in the same
instruction, more registers and sibling calls without call/ret.
BtreeIndexMoveto also has a rotated loop with the condition at the bottom.
Clang's x86 `-S` confirms direct register bit tests, constant stores and
folded memory operands that our x86 still often misses.

The following short, verified attempts are **not kept**: fewer instructions
were not enough for a repeated CPU win.  All table tests are five alternating
`--size 100 --memdb --testset main` runs on CPU 7.  The baseline is
`9fefc28a` in the respective same measurement series; medians in ms:

| Attempt | Baseline | Attempt | Outcome |
|---|---:|---:|---|
| register TEST + address folding + more copy sources | 1296.77 | 1288.80 | not repeated: 1302.94 against 1308.31 |
| plus immediate stores | 1302.94 | 1310.20 | instructions 1.382x to 1.352x clang, CPU worse |
| direct sibling calls with epilogue per site | 1302.04 | 1325.21 | worse |
| sibling calls with shared epilogues for saved registers | 1302.82 | 1318.57 | worse |
| reuse of stack loads within blocks | 1299.70 | 1305.97 | no instruction win in the main functions |

The attempts passed the focused x86 rig (assembly and own object path) and
the SQLite verification hash; sibling calls were also checked with normal
return in the same function, integer/FP return, register pressure, stack
arguments and a local address that must live through the call.  Zero
idioms/narrower constant encoding also gave no safe CPU win and are
reverted.  The kept baseline lies in these series at about **1.15-1.16x
clang**; the earlier size-100 series gave 1.143x.  No new performance win or
reached gate is claimed.  Basis and assembly are in
`work/vir-x86-perf/{arm-compare,testi2,testaddr,framecopies,sti,sib2,sibshared,reload}`.


**2026-11-13: share the optimization basis between the targets.** The history
shows that several general parts live in the ARM lowering and therefore risk
being ported as a second implementation.  The first extraction is now
`vir/facts.c`, with no dependency on the parser, the emitter or CPU
registers:

- `vir_low32`: ARM's backward fixpoint for which uses need only the low 32
  bits, including loop parameters, select, calls and return ABI.  ARM calls
  the common implementation.
- `vir_mask32`: x86's bounded analysis of possible one-bits in an i32
  expression.  x86 uses the common function in its existing extension
  proofs.
- `virtest --selftest` checks mask facts and demand through a loop for both
  full and narrow return.  `vir_low32.c` tests loops, select and narrow/full
  call arguments on both CPUs.

It is an extraction of existing logic: ARM's SQLite assembly and x86's
SQLite object are byte-identical to before the change.  A separate attempt
to use low32 facts for more removed x86 extensions gave fewer instructions
but about 1 % worse CPU time in repeated short speedtest1 tests; it is not
enabled as part of the extraction.

| History | Already common / what should be shared next |
|---|---|
| `ef98fd06`, PR126 | The allocator's loop-entry, argument-move and callee-save rules are already in `vir/alloc.c`; no new x86 port is needed. |
| `6d7fc7f8`, `7d51e03c` | Inlining and dominating reused loads are in the common builder; x86 already uses them. |
| `f1bc6bc1`, `eaa9b8f8` | Now common in `vir/layout.c`, including the copy of the entry test.  Both emitters use the plan. |
| `25157280`, `5f05a106` | The proof that the frame does not escape and that a result is only forwarded to return is IR-common.  ABI, register moves and epilogue/jump belong to the target. |
| `0c2ec7ed` | Matching of field addresses, scale and extension can give a common address form.  ARM's permitted extend/scale and x86's SIB rules are then decided per target. |
| `1795593f` | The save-point analysis is in the allocator; the targets' choice of instructions that need a frame and prologue/epilogue emission remain per CPU. |

Validated: `vir-test` 130/130, the new analysis test in `virtest --selftest`,
focused x86 run via assembly and own object path, ASan/UBSan and
file-by-file build.  The new C test runs on ARM64/QEMU; the byte comparisons
above check both targets' SQLite output.  No runtime win is claimed for the
refactoring.

**Common layout hooked up (2026-11-13).** `vir/layout.c` now owns the
chasing of empty branches (with preserved allocator edits/edge moves), loop
order, rotation plan and decisions on copies of entry test/small joins.
Both ARM and x86 call the same implementation.  The emitters still handle
their registers, instructions, edge moves and frame limits.  No copy of
ARM's planner in the x86 file.  `VCC_VIR_ROT=0` now turns off rotation on
x86 too.

ARM's SQLite assembly is byte-identical.  x86 actually uses rotation with
the copy of the entry test, not just a moved block order.  Five alternating
short speedtest1 samples, size 100 on CPU 7:

| Series | clang CPU ms | Before CPU ms | Shared layout CPU ms | Shared layout/clang |
|---|---:|---:|---:|---:|
| First | 1116.16 | 1283.71 | 1271.89 | 1.140x |
| Repeat | 1112.49 | 1284.06 | 1276.63 | 1.148x |

Instructions/clang: 1.382x → 1.364x.  The improvement over the previous x86
is about 0.6-0.9 % CPU, but **the 1.05x goal is still open**.  The cost is
more code: sqlite3VdbeExec 107831 → 116982 bytes.  A short build
measurement for sqlite3.c via -S is about 0.83 → 0.88 s.  Basis:
`work/vir-x86-perf/shared-layout/{results100,repeat100}`.

Validated: own x86 307/307, GCC 1615/1615, TCC 105/105; inline 83/88 with
the same five known failures.  TSVC 151/151 checksums, 82 real vector loops,
158 functions lowered.  `virtest --selftest` tests order, guard copies,
address-taken blocks, self-cycle, edge moves and edits that forbid chasing.
VIR format tests 130/130.  Existing ARM tests `vir_loop_rotate.c` and
`vir_join_dup.c` are also part of the focused x86 rig (assembly, own object
path, one/six emitter threads).  ASan/UBSan and file-by-file build are
green.

A subsequent x86 attempt to avoid return block copies and fold empty return
blocks directly into the common epilogue is not kept: the first series
1285.08 → 1274.34 ms was not repeated (1275.09 → 1275.76 ms).  Basis in
`work/vir-x86-perf/returnfold`; patch saved outside the checkout.  The
shared layout step above is the kept version.

The next port of sibling calls should in the same way start with common IR
proofs for frame escape and return forwarding, with target-specific
epilogue/emission.

After the rebase on main `06c688f4`, PR126's common allocator rules are
included.  First x86 slice: sequentially consistent atomic loads/stores,
integer mulhi and switch (dense relative tables and sparse intervals) open
e.g. sqlite3VdbeExec for VIR.  Immediate operands and shifts need no
register; a constant shift is chosen from the IR even when the constant
already has a register.  A comparison directly followed by a branch reuses
the flags and can drop the bool materialization.  The condition edges'
register copies are placed outside the main flow; edges without copies go
directly to the target blocks.  ADD uses LEA's three operands; a ptradd with
constant offset and a single immediate memory consumer is folded when no
allocator edits come in between.

**Fast, controlled measurement:** `bench/sqlite/vir-x86.py build --source DIR`
runs VIR via -S + clang's assembler (0.63 s for sqlite3.c instead of the
slow text assembler's object path).  `run clang=EXE baseline=EXE vir=EXE`
runs three alternating perf-stat samples of `--size 30 --memdb --testset
main`, each run under 0.6 s.  All exits and verification hashes are checked;
raw output, binary hashes and medians are saved in summary.json.  The old
rig's timing reached a missing R-tree test and ignored the exit status;
run.py now requires main and a successful exit.  The older wall times are
not a final gate.

| Version | CPU time median | Cycles/clang | Instructions/clang |
|---|---:|---:|---:|
| clang -O2 | 279.05 ms | 1.000 | 1.000 |
| x86 before this slice | 529.27 ms | 1.916 | 2.560 |
| after, including PR126 | 386.78 ms | 1.399 | 1.626 |

Verification hash the same.  **The 10 % goal is still open.**  The next
measurement point is indexed memory operands: more separate address
computations can be removed.
GCC 1615/1615, TCC 105/105, own x86 297/297; inline 83/88 with the same five
known failures.  A targeted rig requires real VIR lowering for
switch/interval, atomics and signed/unsigned mulhi; both -S and own objects
are run.  ASan/UBSan in the targeted rig is green.  TSVC keeps 151/151 and
82 real vector loops; now all 158 built functions go through VIR.

The next slice folds ptradd and adjacent i64 scale into x86's indexed memory
operand (1/2/4/8).  Only one-time values without spills are folded; the
allocator's edits must not overwrite the preserved index.  Effect and copy
passes know base and index.  Targeted run tests cover negative indices,
signed small loads and volatile stores, with a requirement of actual indexed
machine code.  Five short alternating main samples: previous CPU 393.79 ms
(1.380x), indexed 393.38 ms (1.378x); instructions 1.626x → 1.603x.  No safe
CPU win; the 10 % goal is open.  GCC/TCC/own still green, the same five
inline failures.  Measurement data: work/vir-x86-perf/indexed/recheck/summary.json.

The third slice lets the true edge fall through when its block comes next,
drops identity copies at cast and gives destructive operations a tied
preference in the allocator.  i32 promises no upper register bits; a later
EXT must still extend.  x86 now also uses the common VIR inliner's size
analysis and one-time calls (previously explicitly turned off for x86 in
tu.c).  A larger one-time helper call must disappear in the targeted test.
Five alternating samples, same CPU: clang 281.65 ms, previous 383.12 ms
(1.360x), new 356.79 ms (1.267x); instructions 1.603x → 1.505x.
Verification hashes equal.  A separately tried loop layout gave no
measurable win and is not kept.  GCC 1615/1615, TCC 105/105, own 299/299,
the same five inline failures; targeted object/assembly tests and
ASan/UBSan green.  The 10 % goal is still open.  Data:
work/vir-x86-perf/inline-final/results/summary.json.

The fourth slice recognizes two adjacent unsigned byte loads that form a
big-endian word.  Five contiguous instructions with one-time values, no
spills and no intervening edits become one exact 16-bit load and byte
rotation.  Volatile/signed reads and intervening stores are not combined.
Five short samples: CPU 361.49 → 355.25 ms (1.283x → 1.261x clang),
instructions 1.505x → 1.491x.  GCC/TCC still 1615/105 green, own 300/300,
the same five inline failures, targeted tests and ASan/UBSan green.  TSVC
after the inliner slice: 151/151, 82 vector loops, 158/158 functions
lowered.  Data: work/vir-x86-perf/be/results/summary.json; the 10 % goal is
open.

The fifth slice uses CMOV for scalar integer/pointer selects and the same
safe flag reuse as branch.  Both sources survive result overlap; a
comparison that has more users keeps its bool materialization.  Targeted
tests cover signed/unsigned comparison, bool conditions and pointers, with a
requirement of CMOV in real code.  GCC/TCC 1615/105, own 301/301, the same
five inline failures; assembly/object and ASan/UBSan green.  SQLite
instructions 1.491x → 1.482x, CPU unchanged (360.55 → 360.41 ms; clang
284.89 ms).  No proven CPU win from this slice.  Data:
work/vir-x86-perf/cmov2/results.

The sixth slice also folds add.i64 + i2p into a memory operand and
recognizes exactly two/four ordinary byte loads in a big-endian word.  Four
bytes become a 32-bit load and bswap; the same rules on one-time values,
spills and edits apply to the whole sequence.  An indexed byte word can keep
base/index through the removed instructions; no over-read or change of
volatile accesses.  The switch table also uses an indexed load directly.
Five short samples: CPU 351.27 → 348.80 ms (1.251x → 1.242x clang),
instructions 1.482x → 1.461x.  The same GCC/TCC/own 1615/105/301 and five
inline exceptions; extended negative indices/32-bit words, assembly/object
and ASan/UBSan green.  Arithmetic directly from a spill slot and a doubled
inline limit gave no safe win and were not kept.  Data:
work/vir-x86-perf/be4/results/summary.json.

The seventh slice gives identity conversions a tied preference, recognizes
when an i32 definition already zeroes the upper register bits and uses a
bounded bit-mask analysis for non-negative signed extensions.  Parameters,
call results and identity truncations are not assumed to have clean upper
bits.  Signed loads are already 64-bit extended, but a spill slot can break
this: only values without a spill slot may reuse the sign extension.
Targeted tests for dirty SysV registers/returns, truncation, masks and
negative byte values go via assembly/object and ASan/UBSan.  GCC/TCC
1615/105, own 302/302, the same five inline exceptions.  Five short samples:
instructions 1.461x → 1.439x clang, CPU 348.58 → 348.45 ms (1.245x →
1.244x): no safe CPU win.  Data: work/vir-x86-perf/ext2/results.  Default
size 100 before this slice: three samples give 1.249x CPU; the short size-30
test is representative.

The eighth slice lowers constant small memcpy (at most 128 bytes) and
zeroing (at most 256 bytes) without REP's fixed registers and clobbers: SSE2
for whole 16-byte pieces, scalar 8/4/2/1-byte tails without over-read.
Larger, dynamic, volatile and non-zero fills keep the earlier path.  Five
short samples: CPU 347.99 → 344.40 ms (1.233x → 1.220x clang), instructions
unchanged around 1.439x.  Targeted tests cover boundary sizes, unaligned
addresses, tails, 256-byte local zeroing and fallback paths.  GCC/TCC
1615/105, own 303/303, the same five inline exceptions; assembly/object and
ASan/UBSan green.  Data: work/vir-x86-perf/block/results/summary.json; the
10 % goal is open.

The ninth slice folds an ordinary unsigned load, constant AND and eq/ne
against zero into TEST directly on memory, with the exact original access
width.  The two dropped values may have only one user and no spills/edits.
The comparison's bool value is materialized when needed; a following
branch/CMOV can reuse the flags.  Volatile/signed or shared values are not
combined.  Targeted tests cover byte/word/doubleword/64 bits, index and
field displacement, sign-extended masks, shared bool and the fallback cases.
GCC/TCC 1615/105, own 304/304, the same five inline exceptions;
assembly/object and ASan/UBSan green.  Five short samples: CPU 345.64 →
343.66 ms (1.223x → 1.216x clang), instructions 1.439x → 1.433x.  Data:
work/vir-x86-perf/testm2/results/summary.json.

The tenth slice uses a virtual frame base when -fomit-frame-pointer applies.
The last printing pass recomputes it to rsp, including temporary outgoing
stack arguments.  rbp becomes a sixth allocatable callee-save.
Prologue/epilogue and CFI follow rsp; ordinary pointer values in rbp are
told apart from the frame base.  Local and spill slots have the same
conceptual offset as before; -fno-omit-frame-pointer keeps the older path.
Five short samples: CPU 347.82 → 333.56 ms (1.240x → 1.189x clang),
instructions 1.433x → 1.389x.  GCC/TCC 1615/105, own 305/305, the same five
inline exceptions; targeted backtrace through stack arguments, dirty
preserved rbp and both frame choices, assembly/object and ASan/UBSan green.
Unity and file-by-file build and run the targeted tests.  TSVC: 151/151, 82
real vector loops, 158/158 functions lowered.  Data:
work/vir-x86-perf/spframe/results.  The 10 % goal is open.

The eleventh slice can allocate r10 and use r11 as a general lowering
scratch.  Calls then declare r10 as a caller-clobber.  A dense switch gets a
fixed, dead selector in r10; sparse/shared switch values keep the earlier
reserve.  If the allocator's edge copies need their GP scratch at the same
time, the function is recomputed with two reserved GP registers.  Scalar
fabs/fneg now builds SSE2 masks without a GP scratch.  A rotation test with
more live values than registers and bit-exact zero/NaN/infinity signs covers
both FP widths.  Five short samples: CPU 331.41 → 325.35 ms (1.182x →
1.161x clang); instructions 1.389x → 1.388x.  GCC/TCC 1615/105, own 306/306
and the same five inline failures; assembly/object and ASan/UBSan green.
TSVC 151/151, 82 vector loops and 158/158 functions lowered.  Data:
work/vir-x86-perf/r10/results/summary.json.


### V2/V3 x86-64 (2026-11-12): TSVC and five real corpora

After the first leaf slice, TSVC_2, SQLite 3.50.4, PostgreSQL 18.6
(including psql), CPython 3.13.2 and zlib 1.4 were tried on Linux x86-64.
Isolated build trees and all raw diagnostics are under
`work/vir-x86-corpus/`; versions, commit ids and the compiler's hash are in
`manifest.json`. `bench/vcc-vir-corpus.py` is a VPP wrapper that forces
x86-64/-O2/-g0, runs the verifier and the allocator check, and saves the
command, working directory, time taken, errors and VIR coverage per
compilation. `--summary DIR` writes `summary.json`; expected configure probes
are kept apart from real TU errors. Use it as
`VPP=$PWD/bench/vcc-vir-corpus.py VCC_VIR_CORPUS_LOG=DIR` with the projects'
existing build rigs. System include paths can be given with
`VCC_VIR_CORPUS_SYSINCLUDES` (otherwise the LLVM/Linux paths are discovered).

**Changed after the findings:** real SysV calls (direct, indirect and
variadic calls; separate integer/SSE register banks and stack arguments, up
to 23 arguments), call clobbers and FP spills; register-save areas for
variadic *definitions* are still missing. `returns_twice` and the setjmp
names take the earlier path. FCMP follows the NaN rules; scalar select,
integer/FP conversions, fneg/fabs/fsqrt, packed compare/mask select,
i32x4 multiplication on SSE2 (`pmuludq` + repacking), i32x4->f32x4 and
ordered lane-wise i64x2->f64x2 are lowered. Memcpy/memset use rep
movsb/stosb with fixed registers. Unsupported operations are now named in the
reason for the retreat.

| Corpus | Result with the new lowering | Lowered/built VIR functions |
|---|---|---|
| TSVC_2, the pin in fetch-testsuites.sh, 256 iterations | 151/151 checksums vs clang; 82/82 vector candidates reach machine code | 157/158 (s442 has a switch) |
| SQLite 3.50.4 | Testfixture and shell built; new sqlite3.c + speedtest1 run, verification hash same as clang | 1737/1883 in the speedtest build |
| zlib 1.4 | 15/15 library TUs; example/infcover, minigzip round trips and same sizes/CRC as clang | 147/154 |
| PostgreSQL 18.6 | 1504 C compilations, 13 rejected on intrinsic gaps; a strict vcc build is thus not green | 24411/26439 |
| CPython 3.13.2 | 334 C compilations, four rejected; also separate link gaps in mimalloc | 12239/13044 |

The counts are instrumented function instances, not a percentage of all
source functions (the builder can also decline). No verifier or allocator
check reports errors. PostgreSQL is linked separately with clang only for the
13 rejected compilations: psql/postgres --version work, meson gives **81
passed, 0 failed, 249 skipped**. This is explicitly a mixed build, not an
approved vcc corpus or a closed G2/G4 gate. CPython cannot be linked even
after the corresponding four clang compilations; see TODO T13.

TSVC's original five lowered functions became 117 with the calls (41 real
vector loops), then 157 (all 43). The rig now distinguishes the vectorizer's
*candidates* from loops in functions actually lowered; the earlier report
could count a vector loop whose whole function fell back.
**The next vector slice reaches 82 of 125 reviewed loops, all in lowered
functions; 151/151 checksums.** From 43 real vector loops before the change:
strided loads/stores with lane-wise gather/scatter, indexed loads only when
written objects are provably other symbols, first-order recurrences (also two
delayed values), ordered floating-point sums, chained reductions and a
coupled max/index reduction. New `vinsert`/`vextract` are typed, verify the
lane index and are lowered with SSE2/NEON. The floating-point reduction
extracts lanes and counts in scalar order: no reassociation or fast-math.
The dependence check separates the stride's residue classes and counts the
whole address range including gaps in the runtime alias test.

If-conversion reuses exact expressions and already-read values across the
condition arms only without an intervening write/call; several phi values
become selects together. No new speculative memory read. Scalar x86
division/remainder is also lowered with RAX/RDX clobber and a separate
divisor. TSVC's summed loop times on a shared host: before, direct 3.87 s,
VIR 2.36 s, clang -O3 0.84 s; after, direct 3.84 s, VIR 1.67 s, clang
0.88 s. One short run each, no quality gate. What remains is mainly real
memory dependences and conditional stores, not loosened dependence
requirements.
SQLite speedtest1 size 10: VIR 0.25 s vs clang 0.13 s, same hash; the switch
retreat still covers the large dispatch functions. Zlib, best of one CPU
run: direct 610 ms, VIR 626 ms, clang 359 ms; correctness is good, general
code quality remains.

New runtime regressions test stack/indirect/variadic calls and values live
across them, NaN/inf/signed-zero in scalar/packed compare, mask select,
modulo multiplication and 32-/64-bit vector conversion. The rig requires
that these functions really are lowered and that the SIMD instructions are
present, with vectorization off/on, one/six emitter threads and both clang's
and our own object path. Unity/nonunity, ASan/UBSan and TSan are green in
the focused rig. Own x86 runtime tests **285/285**, GCC **1615/1615**, TCC
**105/105**; inline **83/88**, the same five known failures. The focused C
tests also run on arm64/QEMU. The whole of TSVC there: **151/151**, **77**
real vector loops (the targets' builder forms differ; x86-64 gives 82). The
VIR format tests now cover insert/extract and reject wrong lane indexes and
lane types; 300 ASan mutants survive without a crash. The sanitizer also
found signed negation of INT64_MIN in the assembler's expressions: the
negation is now unsigned modularity, new object test; vas-obj **61/61
byte-identical**, also ASan/UBSan.

Final corpus rerun: zlib is rebuilt through VIR and passes the self-test,
sizes and CRC. SQLite exposed a dead scalar result in pageFreeArray: the x86
lowering now uses a reserved scratch register when the allocator gives
VR_NONE, so neither invalid register text nor a crash in -S can occur.
SQLite speedtest1's verification hash is again the same as clang's (0.26 vs
0.14 s at size 10). Arm64's existing switch result-table test found that the
lowering discarded the builder's table queue before end_function; the queue
is now kept until emission, and the seven targeted switch tests pass. All of
arm64's own runtime suite: **290/290**, one known xfail.

Next backend steps: switch, mulhi and more ABI groups, followed by SIMD
records/address folding and more loop shapes. The intrinsic/inline gaps
found are their own front-end/ABI items, not reasons to hide fallback in the
project builds.


### V2/V4 x86-64 (2026-11-12): first allocated leaf functions and SSE2

`VCC_VIR=bl` on x86-64 now goes through the same builder and loop vectorizer
as arm64; the function is marked `x86_64-linux`. `arch/x64/codegen/vir.c`
lowers scalar leaf functions with SysV's integer/FP registers and stack
parameters, fixed `cl`, callee-saved registers, spills and sequenced edge
moves. Frame and spill tables are reused per `ecx`. `VCC_VIR_CHECK=1` checks
the real target allocation. The mode caches for VIR, vectorization and
compare merging are thread-local; the six-thread test found and removed a
race in the last of them.

The SSE2 slice covers 128-bit loads/stores, float/double arithmetic, integer
addition/subtraction and bit operations, splat, index sequence, reversed
lanes and integer reduction. The shared vectorizer keeps its alias and
remainder-loop checks. A counter in `cl` must survive even when the result
gets `rcx`; the regression tests this specifically. SIMD is for now written
as opaque `X_RAW` records: correct but conservative for the post-optimizer.

`bench/vcc-vir-x86.py` requires nine actually lowered functions and five
loops with SSE2 instructions; they run both with and without vectorization,
via clang's assembler and our own object path. The test compares one and six
emitter threads and requires identical `-O0/-O1` text with VIR off/on. Unity,
separate-file build and ASan/UBSan are green (also the whole own x86 suite:
282/282). Linux x86 run suites without cache with VIR + allocator check:
**1615/1615 GCC, 105/105 TCC, 282/282 own**. The inline suite is **83/88**:
the same five failures and 22 exceptions with VIR off as on; no new
failures. The new own test also runs on arm64/QEMU. The 22 own `vir_` tests
also pass with `-fPIC`. The targeted test is green under TSan with six
emitter threads and runs in the existing x86-hosted TSan CI job.

Short micro-measurement on this shared x86 host: 4096 float elements,
`a[i]=b[i]*c[i]+1.5f`, 10000 calls, strict `-ffp-contract=off`, median of
seven alternating runs (CPU time): direct **26.931 ms**, VIR **7.709 ms**,
clang -O2 **2.427 ms**, same checksum. VIR is 3.49 times faster than the
direct path but still 3.18 times slower than clang on this particular test;
no general quality gate is reached. Warm process compilation of the small
TU, median of eleven: direct **2.86 ms**, VIR **3.27 ms**, clang
**18.12 ms** (process start included). Sources, assembler and all samples
are in `build/linux/vir-x86-results/measurements.json` and its directory.

**Remaining for V2/V3/V4:** calls, more ABI groups, varargs, division, more
scalar operations, vector multiplication/compares/conversions and mask
select. Unsupported functions use the earlier path. VIR is still explicitly
switched on and only for `-O2`; neither the x86 coverage gate nor V4's code
quality gate is closed. Next code quality steps: own SIMD records, constant
shifts without `cl`, address folding and more efficient loop branches.


### Vectors and loop vectorization (2026-11-12): first slice, TSVC_2 151/151

Basis and design: `RESEARCH-vectors.md` (changes SPEC 1.3/1.4: VIR has its
own vector values, and a loop transformation).  Done, in this order:

- **The builder did not take floating-point arguments.**  An `f32` in the
  argument slot's 8-byte word was stored as `f64` (`vw_store`), so every
  function that called something with a `float` parameter fell back.
  `tsvc.c`: 7 of 158 functions built before, 158 after.
- **`bench/vcc-tsvc.py`**: TSVC_2 (`fetch-testsuites.sh TSVC_2`), checksums
  of 151 loops against clang -O3, direct emitter and VIR; `--why` gives the
  vectorizer's reason per loop.
- **Types and operations** (SPEC 4, 7.5): `f32x4`, `i32x4`; the scalar
  operations take them through `OK_T`; `vsplat vseries vicmp vfcmp vbitsel vs2f
  vrev vreduce`.  Mask = `i32x4` with 0/-1.  Vectors in the floating-point
  class, spill slot 16 bytes (`4 << class`).
- **arm64 lowering** (`codegen/vir/a64.c`, `vl_vec`): NEON as encoded words,
  checked against clang's assembler; register move and swap are `mov`/`eor
  v.16b`.  **A vector in a spill slot is not lowered** (the function then
  takes the usual path): there is no record for `ldr/str q` against the
  frame.
- **`nsw`** on signed `+ - *` from the walker; `-fwrapv`/`-fno-strict-overflow`
  (`cc_opts.wrapv`) turns it off.  Only the vectorizer reads the flag.
- **The vectorizer** (`codegen/vir/vec.inc`, on the pre-IR after
  if-conversion): innermost loops with the test in the header and a straight
  body; inductions and `i32` sums; loads/stores `symbol ± 4·t`; the
  dependence rule in RESEARCH 6.3; the scalar loop stays as the remainder.
  Off under `-mgeneral-regs-only` and with `VCC_VIR_VEC=0`; `VCC_VIR_VEC=r`
  reports.

Measured (M1 Pro, `iterations` 256): direct emitter 7.2 s, VIR 4.3 s (5.1 s
before the vectorizer), clang -O3 1.5 s; 151/151 checksums on all three.
**28 of 120 inner loops vectorized** (clang: 77 loops in 66 functions).
The vectorized loops run 5-10x faster than the direct emitter and 1.3-5x
slower than clang's (no interleaving, addresses are recomputed in the loop).
Reasons for the rest: floating-point reduction or recurrence 17, store with a
step other than the element 16, call 11, address that is not symbol + linear
(pointer) 10, stored value read 1-3 iterations later 10, value that cannot
become lanes (gather, `double`) 9, wrong order within the body 6.

Validation: `test/vcc/vir_vec_loops.c` (47 loops x trip counts 0-9 and 37
against clang: remainder 0-3, downward-running, distance -5..5 within one
array, store before load, sums, select, invariant from memory);
`vcc-ctests.py` with `VCC_VIR=bl` 2079/2082 (the three `.Lswitch.values`
failures existed before), unchanged without VIR; ASan/UBSan clean on TSVC
and the test; `vir-test.py` 124/124.

**Second round (same day): 43 of 120 loops, VIR 2.6 s.**

- **Runtime overlap check.**  Two accesses that are neither one object nor
  two symbols (pointer against anything, invariant load against a store, an
  array at unknown distance `a[i]`/`a[i+k]`) are compared as byte ranges in
  the VPH; if two meet, the loop runs scalar.  At most 8 pairs (as LLVM).
- **Pointers** as base: parameters, and addresses computed as integers
  (`i2p` of a loaded global pointer + index).
- **Pointer inductions** in the vector body instead of a recomputed index.
- **Inductions**: several steps per iteration (`j++` twice), a step that is a
  value (versioned on step = 1), unsigned `i32` counter with step 1 under an
  unsigned bound, `size_t`.
- **Vector spills are lowered** (`ldr/str q` through x16, slot to slot
  through v31): no more fallback to the direct emitter.
- **d8-d15 for scalar floating-point values across calls**
  (`codegen/vir/a64.c`): offered to the allocator as call-preserved, saved at
  the entry and restored before every return and sibling call.  Not when a
  vector lives across a call (`vl_vcross`; none does today) and not with
  `-fasynchronous-unwind-tables` (CFI for the slots is missing).  A
  floating-point accumulator in a loop inside a loop with calls went through
  memory every iteration: s311, vsumr, vdotr 0.45 s -> 0.08 s (clang's time).

Measured: direct emitter 7.5 s, VIR 2.65 s, clang -O3 1.49 s; 151/151.
Reasons for the 77 loops that remain: step != element 17, floating-point
reduction/recurrence 17, value that cannot become lanes 10, stored value
read 1-3 iterations later 10, address not base + linear 6, call 6, order in
the body 6.  Of the 66 functions clang vectorizes we take 36; the biggest
difference: 9 with conditional store (clang scalarizes the store per lane),
7 floating-point reductions (clang: ordered), 6 with a step known only at
runtime in a multiplication.

Tests: `test/vcc/vir_vec_loops.c` (now also overlapping pointers, 10 spilled
vectors, steps 1 and 3, unknown distance, unsigned counters),
`test/vcc/vir_fp_callee.c`.  `vcc-ctests.py` with `VCC_VIR=bl` 2080/2083
(the same three), without VIR 2083/2083; ASan/UBSan clean.

**Third round (same day): scalar loops.  VIR 6.3 G cycles, clang 4.75.**

- Floating-point values are loaded and stored directly in v registers
  (`ldr/str s/d`) when the address is register + offset or register +
  index; before, via x16 and `fmov`.
- `s2f`/`u2f` of an integer constant and `fext`/`ftrunc` of a floating-point
  constant are folded.
- **Loop constants are hoisted** (`vv_pin` in `vec.inc`): symbol addresses,
  floating-point constants != 0 and comparison bounds that are not an
  immediate are made once in front of the straight inner loops the
  vectorizer looks at anyway (at most 6, no loops with calls).  **This is a
  departure from "No LICM in 1.0"** above, limited to constants;
  `VCC_VIR_VEC=n` turns it off.  Effect on TSVC: -19 % instructions, -7 %
  cycles.  **Decision (Johan, 2026-11-12): kept**, as an exception in the
  pass list above; broader LICM is still outside 1.0.
- `bench/vcc-tsvc.py` writes cycles (`/usr/bin/time -l`): the machine is
  shared and seconds vary.

Measured in cycles (G): direct emitter 23.6, VIR 6.33, clang -O3 4.75;
151/151.

**Fourth round (same day): 8-byte lanes and select.**

- **`f64x2` and `i64x2`** (SPEC 4, 7.5; the operand kinds
  `OK_VI`/`OK_VF`/`OK_VRED` follow the result's lane width): types, table,
  verifier, NEON (`.2d` is `.4s` with the size bit), and the vectorizer is
  generic over lane width (the width is the loop's first store; VF 2 or 4).
  No 64-bit multiplication; a loop that mixes 4- and 8-byte data runs
  scalar.
- **Select (`select`)**: comparisons -> `vicmp`/`vfcmp` -> `vbitsel` had never
  been tested, because the tests' selects had loads in the arms and were not
  if-converted.  Now 16 comparisons x two lane widths against clang.  Two
  holes this showed: (1) **the builder did not take `float` condition
  expressions** (`c ? x : y` with `float`: the temporary's 8-byte word was
  read as `f64`; `vwslot.f32w`), so such functions did not go through VIR at
  all; (2) integer selects come as `trunc(select i64(ext, ext))` from the
  walker and are now recognized (`VK_W32`).
- Loop rotation in the lowering (the test copied to the end of the loop) was
  tried: -1.3 % cycles on TSVC, 0 on sqlite, +0.9 % code.  Not introduced
  (the same conclusion as earlier in `vl_dupok`).

`test/vcc/vir_vec_wide.c` (51 loops on 8-byte lanes x trip counts 0-9 and
37).  TSVC unchanged (everything there is `float`): 43 of 120, 6.3 G cycles.
SQLite's speedtest1 through VIR: same verification hash as clang, 2.29 G
cycles (clang 2.01, direct emitter 3.05); 5 of 565 inner loops are
vectorized there (the rest are list walks, calls and byte loops).

Remaining (RESEARCH 6.6 and 5): `i8x16`/`i16x8` (byte loops: require that the
i32 count may be narrowed); `double` from `int` (`(double)i`); invariant
expressions that are not constants; CFI for d8-d15; step !=
element size;
interleaving; ordered floating-point reductions; the other four lane types;
x86-64 and wasm; the interpreter and `virgen` do not know vectors; the
kernel, PostgreSQL and SQLite have not been run with VIR after this.

### V0 (2026-11-11): ready for review

- `SPEC-vir.md` (draft 0.1): the whole format (16-byte instruction, blocks,
  stack slots, `extra`), types, memory model, all 1.0 operations with
  effects, ABI signatures, text form with grammar, the verifier's 27 rules,
  the interpreter, the passes' contracts, the register allocation interface,
  tools, tests and limits.  It lists seven changes required outside `vir/`
  (X1-X7) and seven open questions (O1-O7).  The plan's Design section above
  is a summary; where they differ the specification governs (e.g. only `br`
  carries block arguments, and `__builtin_constant_p` is decided by the
  walker).

- `RESEARCH-vir.md`: 29 problems in LLVM IR in three groups (semantics,
  structure and compile time, the C interface) with sources, and the rule VIR
  takes from each; a table of other compilers' choices.
- This plan.  The first draft (approved in plan mode) started from a branch
  without x86-64 and had its own AST builder and arm64 as the first target.
  After reading `main` the builder is instead a virtual `LO` target, x86-64
  is the first target, and VIR sits at `-O2`.
- No code changed.

### Preparation (2026-11-11): X1-X3 on `main`, without changed code

The documents are on `main`, and three output-neutral changes in the walker
are done (SPEC-vir.md section 19, X1-X3); nothing under `src/vir/`.

- **X2:** `LO(slot)(e, off, size, align, kind)` is called once per issued
  frame offset (`wk_pslot`, both reuse paths in `wk_slot`, `wk_local`; `kind`
  is `WK_SLOT_TEMP/LOCAL/PARAM/FUNC`).  `x64_slot` is empty.  The static
  helper function `x64_slot` in `fp.c` was renamed `x64_tmpslot` (a name
  clash the spec did not see).
- **X3:** `L_VAR` in the `lval` enumeration; `x64_load`/`x64_store`/`x64_laddr`
  give an internal error.  Nobody produces it yet.
- **X1:** `src/tools/gen-walkprefix.py [--check]` generates
  `codegen/walk/prefix.h` (130 names); `WK_PREFIX`/`LO_PREFIX` give a second
  instance.  Proven with a one-off build (not checked in): `walk/*.c` with and
  without `-DWK_PREFIX=vw_ -DLO_PREFIX=vw_` give 46 global symbols each
  without overlap, no unprefixed `wk_`/`x64_` references left, 74 `vw_`
  operations as a virtual target is to provide.  No second instance has been
  added; it must become its own TU (not `emit` unity).
- **Verification** (base = the commit before the code changes, `vbench.py
  mkbase`): x86-64 `-S` O0/O1/O2 on 253 test files (759 compilations)
  identical; `vcc-same.py` 14687/14693 (the 6 deviations are `-E` line
  markers with the base build's own include path, other modes identical);
  `vbench.py check` 171/171; `vcc-ctests.py --suite vcc` 252/252 (1 xfail);
  `vcc-sema.py batch` 878/879 (modpost.c, as expected); `vbench.py gate
  --base` ok (at most +0.09 % instructions).  Both builds (unity and
  `VCC_UNITY=`) without new warnings.

### V1, first half (2026-11-11): format, text form and verifier

V1 parser: an empty block body now gives verification error S3 without first
calling `memcpy` with a null source and length zero. Regression test and the
whole VIR suite pass under ASan/UBSan (119/119); mutation test with seed 42
passes 1,000/1,000. The other V1 gates and the VIR steps remain.

Done: `src/vir/` (module `vir_SRC`, its own unity TU, linked only into
`virtest`; nothing in `vpp`, `vccd` or `codegen/` changed).  `vir.h` (format,
opcode table that the printer, parser and verifier share), construction with
hash-consed constants, `vir_cfg`, `vir_dom` (Cooper-Harvey-Kennedy, pre/post
per block), `vir_uses`, `vir_canon`, `print.c`, `parse.c`, `verify.c` (all 27
rules of SPEC section 12), the tool `virtest` and `bench/vir-test.py`.
SPEC corrected to draft 0.4 in its own commit (list in the PR).

- **Tests:** 118/118 (`src/test/vir/`): 9 operation files where every
  instruction and flag is written-read-written identically, 56 verifier
  cases (at least one per rule, exactly that rule's id), 39 syntax errors, 7
  canon cases, 6 dominator cases (three irreducible graphs) and `--selftest`
  for the builder API.
- **ASan+UBSan** (`make vcc-asan`): 118/118, and 9000 random mutants of the
  test units (`--mutate`) without a crash.
- **Default output unchanged:** `vbench.py check --base` (base =
  `origin/main`) 171/171 identical; `vbench.py gate --base` ok (at most
  +0.07 %).  Both builds (unity and `VCC_UNITY=`) without new warnings.
- **Remaining of V1:** the interpreter `virrun`, `virgen`, `fuzz-vir`,
  `--equiv` and the microbenchmarks in SPEC 22.8.  No passes exist, so
  `PASSES:` accepts only `canon`.

### V4, first part (2026-11-11): the allocator and the check on a synthetic target

Branch `claude/vir-regalloc-impl-f22a1d` on `main` (V1's first half); no code
in V1 changed.
- **Done:** SPEC 15.1-15.2 as an interface (`include/vir/alloc.h`: `vir_regs`,
  `vir_fix`, `vir_alloc_out` in the thread's reservations), the loop forest
  as intervals of the block sequence (`loops.c`, `vir_loops_build`, replaced
  by `vir_canon` in V2), `vir_alloc_check` (`allocchk.c`, written before the
  allocator), a synthetic target (`synth.c`: N integer and M floating-point
  registers, callee-saved subset, x86-like fixed registers: two-address,
  shift count, div, calls), sweeps 5 and 6 (`alloc.c`: linear mode, hints,
  loop corrections, shared spill slots, the main rule, calls, constants,
  parallel moves with cycle breaking via a free register, `xchg` or
  scratch), text dump and the tool `viralloc` (`allocdump.c`, `viralloc.c`:
  dump and counters, `--sweep`, `--time`, `--mutate`).  Inline asm (15.10)
  and the `dce` merge are not included.
- **Tests:** 16 files in `src/test/vir-alloc/` with expected dumps
  (straight code, diamond, nested loops, values live throughout, critical
  edges, block arguments that swap and rotate, calls, fixed x86 registers,
  `ibr` to `VB_ADDR`, irreducible, linear mode); each file is run at every
  register count down to the minimum (3 integer, 2 floating-point), three
  callee variants, x86 off and on and linear mode: 618 configurations, the
  check green on all.  `bench/vir-alloc.py random`: 1000 random units (8000
  functions, seeds 1-600 and 1000-1299) green, 60 units with ASan+UBSan
  green; `mutate`: the check catches 2869 of 2979 corrupted allocations (the
  misses are `br` swaps that are only information and dead results).
- **Bugs the random tests found, all in irreducible flow and now rules in
  SPEC 0.5 with one test each that fails without the rule:** `lend` must be
  set at each back edge's target, not only the loop header
  (`irr-merged.vir`); an edge into the middle of a loop brings along the
  values that live around the loop (`irr-entry.vir`); a value that lives
  around a loop with a block that starts in stack slots needs a slot
  (`addr-loop.vir`).  Also: a callee-saved register that only one edit
  writes (`x86-shift.vir`), two-address without a free register (the first
  operand is evicted), an unnecessary self-swap after `xchg` (`rotate.vir`).
- **Time (best of five, M1 Pro, shared machine):** interpreter-shaped
  function (`big_interp`: 2927 instructions, 514 blocks, 25 block parameters
  in the loop) 177 ns/instruction at 14+14 registers, 172 at 6+4; random
  functions 81-83 ns.  Under the budget of 200 ns that applies for now (SPEC
  22.1, decision 2026-11-11: code speed in sqlite first); the target of 60
  ns stays until after quality.  Profiling gave -13 to -16 %: thread-local
  access via pointer (`_tlv_get_addr` cost 3-4 %), no cleared register field
  per block, no scoring or edge move for a sole predecessor, operand lists
  from sweep 5 to sweep 6, operand fields without an iterator, a queue list
  for parallel moves.  Remaining: cost per block proportional to the number
  of live values (W is rebuilt, the live-in pool, entry state), eviction
  (distance per register), `fixed()` per instruction and `br` with 25
  arguments.  The interpreter shape is the worst case (5.7 instructions per
  block); the figure on real code comes with V2.
- **Counters on the test set** (at each file's top register count): 27
  spills, 34 reloads, 17 rematerializations, 15 edges with moves, 2 `xchg`,
  7 callee-saved.  The interpreter shape at 14+14: 697 spills, 1617 reloads,
  257 edges with 1183 stores to stack slot (the loop's parameters start in a
  slot by the main rule: a quality problem to measure against clang in V2).
- **Goal for V4 as a whole:** `bench/sqlite/run.py all` (on another branch):
  speedtest1 gcc 2.55 s, clang 2.62 s, vcc 4.01 s; the VDBE interpreter is the
  shape the allocator is to do better on.  Cannot be run through VIR until
  V2.
- **What got in the way in V1's format:** the parser does not set
  `blk.depth`; `vir_cfg` rewrites `extra`/`nextra` (the allocator calls it
  every time); `vir_term_of`/`vir_succ_csr` are `VPRIV` and static in the
  unity build, so the allocator builds its own successor lists; the format
  does not say where a `proj` stands (the allocator requires that it follows
  its call directly) or that block 0 begins with its `param`s; no loop
  forest.
- **Waiting on V2:** real `vir_regs` for x86-64 (and arm64), the lowering
  15.3, C as input, quality against today's `-O2` and clang, sqlite.  **V5:**
  `dce` in sweep 5.  **V5b:** inline asm (15.10).
- **Default build unchanged:** `bench/vbench.py check --quick` 66/66
  identical to the snapshot (`2d122cf1`) and to the commit before it
  (`1327df46`); against the merge base with `main` 15 differ, from the
  unmerged branches below this one, not from it.  `bench/vir-test.py`
  118/118.

### V4, webs (2026-11-12): block parameter and argument as one variable

Step 1 in the work order for sqlite (SPEC 15.5 and 15.8, "Webs").
- **Done:** a block parameter and its `br` arguments are a web (union-find).
  Two webs with parameters in the same block are not merged.  A member that
  is live where another is defined leaves the web; at a parameter the live
  ones leave instead.  The web has a shared spill slot outside the ring and
  a preferred register (the first parameter's).  A parameter that crosses a
  call before it is used, or is only passed on, starts in the web's slot.
  Lifetimes around loops are now exact intervals per value instead of `lend`
  as a linear bound, and `livein`, `dmark` and the webs' interference read
  the same intervals.  Two precision rules: a value whose definition does
  not dominate the loop header lives only to the end of its group of back
  edges, not around the whole loop; and the rule for an edge into a loop
  takes only values whose definition dominates the source block (strict
  SSA).  That rule now also applies to an edge to the header (irreducible
  loop where the use lies behind an inner back edge; `irr-head.vir`, found by
  random seed 61, fails without the rule).
- **sqlite3VdbeExec:** stack accesses 5197 -> 2587 (the goal was under 1500).
  The blocker is `add x25, x25, #24` in place; its predecessor used to
  reload three values and copy `pOp`.  The largest source was the rule for an
  edge into a loop: a value from one case in the interpreter was counted as
  live around the whole interpreter loop.
- **Time:** speedtest1 (size 100): 33.75 G -> 32.48 G instructions, cycles
  8.83 G -> 8.95 G (within noise; ssa-llc2 8.39 G).  Profile at size 300:
  sqlite3VdbeExec 2094 samples (before 2115; LLVM back end ssa-llc2 2228; the
  goal of 1900 corresponds to the build with inlining only, 1818: requires
  step 2).  The stack accesses thus cost little: store and load on the stack
  are forwarded.
- **Tests:** vir-alloc 17/17, random 1300 units (seeds 1-1000, 1000-1299)
  green, `mutate` catches 2871 of 2980; vir-test 118/118; the C suites
  2064/2064, through VIR with the check 2058/2064 (the same six known
  failures), inline through VIR 85/85, `VCC_UNITY=` builds, TCL through VIR
  1 failure (interrupt2, as clang); sqlite through VIR with the check: 1664
  functions, hash unchanged.

### Per-call-site inliner (2026-11-12): size bottom-up, no growth budget

Step 2 in the work order for sqlite.
- **Done:** the prepass (`prepass.c`, `fn_isize`) collects the direct call
  graph and computes per function `isz`: its own size plus `isz` for each
  called function that would itself be inlined (as LLVM, bottom-up).
  Recursion groups (Tarjan's strongly connected components) are not counted
  and not inlined within themselves, so the choice does not depend on the
  order of the threads.  Under VIR a called function is inlined when `isz <=
  WK_INLINE_VIR` (150 AST nodes); static functions with a single call as
  before (4000).  The growth budget per caller (`inlgrow`, 400) is gone: it
  was what held down the nested copies, and it made the choice depend on the
  order in the caller.
- **Limit** (speedtest1 size 100, cycles; `.text` of sqlite3.o; compilation
  of sqlite3.c through VIR):

  | limit | cycles | text | compilation |
  |---|---|---|---|
  | 40 + budget (before) | 8.96 G | 913 KB | 0.37 s |
  | 80 | 8.91 G | 1049 KB | 0.42 s |
  | 150 (chosen) | 8.83 G | 1356 KB | 0.52 s |
  | 225 | 8.79 G | 1525 KB | 0.57 s |
  | 300 | 8.78 G | 1620 KB | 0.58 s |

  After 150 each percent of cycles costs more than 10 % code; 150 is the
  knee.  LLVM's build with inlining only (st-inl-only) runs at 7.66 G, but it
  also removes `noinline` (`SQLITE_NOINLINE`: getCellInfo).  Profile at size
  300 (225 vs inl-only): sqlite3VdbeExec 2133 vs 1717, vdbeRecordCompareInt
  641 vs 532, jsonTranslateTextToBlob 230 vs 129: the gap lies in the code
  per function, not in what is inlined.
- **Bugs the inlining showed:** the walker assumed x87 layout for `long
  double` except for `__float128`; on arm64 Linux it is IEEE binary128.  A
  promoted `long double` constant was stored in the wrong format
  (`vcc/ldouble_const` through VIR), and `__builtin_isinf_sign` read the sign
  at byte 9 (`vcc/isinf_sign`, one of the six known failures).  `wk_quad`
  decides the format in one place.
- **Tests:** the C suites 2064/2064, through VIR with the check 2059/2064
  (five known failures left: jump_table, switch_dispatch, switch_values,
  returns_twice, setjmp_sites), inline 85/85 with and without VIR, vir-test
  118/118, `VCC_UNITY=` builds, TCL through VIR 1 failure (interrupt2, as
  clang); sqlite through VIR with the check: 1420 functions, hash unchanged.
  Same run: st-web8 9.31 G cycles, now 9.18 G (machine loaded; 32.50 ->
  32.07 G instructions).

### Allocator quality and addressing modes (2026-11-12)

Step 1 in the new work order: samples per function against virpol (VIR's
inlining, LLVM's back end), not number of stack accesses.  Measured with
paired runs (speedtest1 size 100, cycles).
- **Promotion of a slot with pointer and integer stores:** a local pointer
  whose address an inlined reader takes (`sqlite3Utf8Read(&z)`) is stored as
  `ptr` and, after `p++`, as `i64` (`p2i`, `add`, `i2p`); `vw_promote`
  refused fields with different types.  The field now holds `i64`, and a
  pointer store's value goes via `p2i`.  patternCompare's zPattern/zString
  were in memory.
- **`i2p`/`p2i` are read through** before allocation (the users read the
  operand: a load's address becomes an `add` that is folded in); the
  constant in a commutative operation is placed second (an immediate).
  -1.5 % instructions, -0.5 % cycles.
- **A copy of 1, 2, 4 or 8 bytes** (`memcpy(&v, &r, 8)` in FpDecode) is a
  load and a store, so that both slots are promoted.  -0.2 %.
- **Web parameters:** a web's parameter that lives across a call and is not
  read soon went into the web's slot.  Now not when each successor (at most
  two) reads it near its start (-0.6 %), and not when the register is
  callee-saved and two more are free at the start of the block (-2.2 %;
  patternCompare: sp loads/stores 32/40 -> 1/6; without the condition
  VdbeExec's previously read values lost their registers, +1069
  instructions).
- **Addressing modes:** VdbeExec's gap to virpol (+195 samples) was not stack
  traffic (VIR has fewer samples on `[sp]`: 414 vs 484) but instruction
  selection: VIR had no register-offset addresses (LLVM 2112).  New records
  `I_LDX`/`I_STX` (`[xn, wm, sxtw #k]`), `I_ADDXT`/`I_SUBXT` (`add x, x, w,
  sxtw #k`) and `I_SMADDL`/`I_UMADDL`; the lowering (`vl_cinfo`/`vl_cfold`)
  folds an index built just before a `ptradd`, `add` or `sub` (`ext.4s/4u`,
  `mul`/`shl` with the access's size, `mul` with a 32-bit constant), each
  instruction used once by the next.  Decided at the chain's first
  instruction, where the index's register is known; in between only
  remat/reload to other registers.  Loads/stores: -2.0 % instructions, -1.1 %
  cycles; add/sub/smaddl: -2.5 % instructions, cycles unchanged (the same
  latency as what they replace).
- **Sum:** against step 3, same run: -7.3 % instructions, -3.5 % cycles;
  virpol is now 9.7 % faster (was 12.9 %).  sqlite .text 338547 -> 312681
  instructions, spills 5684 -> 3753.
- **Remaining, measured:** samples on `[sp]` in medium-sized functions
  (vappendf 95 vs 38, BtreeInsert 54 vs 16); f64 that lives across calls is
  reloaded via x16 (no callee-saved FP registers: `vl_regs` sets `callee[1] =
  0`, 107 places); constants are rematerialized in loops (no hoisting).
- **Tests:** `vcc/ptr_slot_inline.c`, `vcc/memcpy_small.c`,
  `vcc/index_addr.c`; the C suites through VIR with the allocation check,
  vcc-cfi and darwin -S through VIR (the encoding equal to clang's assembly
  of the text).

### Register copies around calls (2026-11-12)

Assignment from the comparison against virpol: `mov` between registers was
+313 samples over LLVM (2.0x statically).  The example (`mov x21, x0; b join`
in each branch of vdbeRecordCompareString) was not a block parameter: `v3 =
trunc v0` is bound (in the operand's register when it dies) and stayed in x0,
although it lives through a loop with a call; at the join it was in x21, so
each branch and the back edge moved it.  Three rules in `vir/alloc.c`:
- a bound operation whose result lives across a call in a loop is not
  counted in place in a caller-saved register when a callee-saved one is free
  (one move at the definition instead of one per iteration);
- an `ext` (an instruction in any register) takes its hint's register when it
  is free (all bound: more moves, tried);
- a value whose next use has fixed operands (a call's arguments) does not,
  when the hint is taken, choose a register that another of its operands is
  to go to: memcmp's `mov x3, x1; mov x1, x2; mov x2, x3` becomes `mov x1,
  x3`.  A fallback to the block parameter's own hint when it lacks a
  tentative register was tried: more instructions, dropped.
sqlite: `mov` between registers 37904 -> 35785 (static, the whole binary),
instructions 312681 -> 310218; paired against the previous build -0.1 %
instructions, -0.6 % cycles (within noise for each rule separately).
Remaining of the hot moves, from the profile: the selector in a `switch` is
copied to x17 before the compare (VdbeExec's hot dispatch,
vdbeRecordCompareInt, SerialGet) and `mov w0, w0` before a `ret` (lowering,
left to vir-lowering); a rotation of four loop parameters on a back edge in
VdbeExec; the rest are samples after jumps.
- **Lives across a call, after the live sets:** `XCALL` was a test in
  instruction order (a call between the definition and the last use), so the
  arguments to calls in separate branches (vdbeRecordCompareInt's v1/v2)
  were counted as live across calls and moved to x19/x20.  Now the values in
  W (live after it) are marked at each call; values that live around a loop
  (outside W beyond the back edge) and linear mode keep the order test.
  sqlite: -1074 instructions, -474 `mov`, 17 fewer frames, spills 3787 ->
  3727; paired -1.5 % instructions, -0.4 % cycles.

### Sibling calls, free argument swap and va_arg (2026-11-12)

Step 3 in the work order for sqlite.
- **Sibling calls:** a call whose result (or none) is what the `ret` after it
  returns becomes a jump after the epilogue, anywhere in the body (VIR's
  lowering, `vl_sibling`).  The result may go via `br` to a block with only
  `ret`, via block parameters, `i2p`/`p2i` and, when the function returns a
  32-bit integer, `trunc`/`ext.4` (the upper half is unspecified).  Not when
  a slot's address escapes loads, stores and copies (as LLVM's
  TailCallElim), with stack arguments, asm, `va_start` or a call that
  returns twice; the attributes as for the arm64 emitter
  (`em_tail_caller_ok`, `em_tail_target_ok`).  `I_TAIL` cond 2/3 is expanded
  by `finish_function` to restore, `mov sp, x29`, `ldp` and `b` or `br x16`;
  with unwind tables between `.cfi_remember_state` and `.cfi_restore_state`
  as clang.  sqlite: 481 direct and 51 indirect.
- **Free argument swap:** a value that dies at a call and that a fixed
  argument register displaces now gets a register that the call overwrites,
  and is removed entirely when its copy is already in an earlier argument
  register.  Before, it got a callee-saved register, which gave a frame in
  functions like getCellInfo.  sqlite: spills 6991 -> 5565.
- **Early return before the prologue:** getCellInfo now has no frame at all
  (`ldrh`, `cbnz`, ..., `br x16`; the return is `ret`).  General
  shrink-wrapping (a function that needs a frame on one path) is not done.
- **va_arg and variadic functions (Apple):** `vw_va_arg` moves the pointer (8
  bytes per argument, 16-aligned first, records over 16 bytes by reference),
  `va_start` becomes `add x16, x29, #16+named stack bytes; str`.  `wk_valist`
  took a pointer-typed va_list for x86-64's array; Apple's is a `char *`
  (lvalue).  AAPCS64 (Linux) is refused as before.  sqlite: 19 variadic
  functions and sqlite3_str_vappendf now go through VIR (1432 lowered, 6
  left: records as arguments).
- **Time:** speedtest1 size 100, same run: step 2 9.38-9.42 G cycles, with
  sibling calls and the argument swap 9.28-9.30 G (-1.3 %), instructions
  32.08 -> 31.64 G.  The va_arg coverage changes nothing measurable (9.32 G).
- **Tests:** `vcc/tail_calls_body.c` (sibling call in a branch with a
  callee-saved register, via `br`, indirect, pointer; negative: `(unsigned)`
  widened to 64 bits, local address), the C suites, vcc-cfi through VIR
  1567/1567, vcc-abi `--host` through VIR the same four known failures as
  without.

### Lowering against LLVM's backend (2026-11-12): five points, sqlite speedtest1

Branch `claude/vir-lowering-453a57`.  Measured with `VCC_VIR=bl`, speedtest1
`--size 100 --memdb`, pairwise runs (3 rounds) against the build before the
point and against virpol; counters static on sqlite3.o.  From the start
(5346e3e6) +14.5 % cycles against virpol, now +9.9 %.  Samplings per
function (size 300) vary +-10 % with the machine's load and are reported
only as a direction.

| Point | Commit | Counters before -> after (virpol) | Instr. | Cycles vs previous | Vs virpol |
|---|---|---|---|---|---|
| 1 branch selection: tbz/tbnz, cbz/cbnz against zero and sign, repeated compare removed | 71ccdf3c | tbz+tbnz 0 -> 2677 (2645), and+cbz 2690 -> 497, cmp 13976 -> 13072 | -3.0 % | -1.9 % [-2.1 .. -1.6] | +12.0 % |
| 2 block order and exits: return with its own epilogue, lone `br` copied, leaf frame without frame record | ad813ee8 | b 20213 -> 18915 (12145), ret 1473 -> 2771 | -0.7 % | -0.9 % [-2.5 .. -0.4] | +11.0 % |
| 3 arithmetic: register offset, shifted/extended operands, madd/smaddl, ubfx/sbfiz, multiplication by constant | dabc8f08 | register offset 0 -> 2567 (2675), extended 0 -> 1023 (1283), smaddl 0 -> 1753 (1855), sxtw 5509 -> 1252 (1629), mul 2892 -> 383 (228) | -5.6 % | -1.2 % [-2.7 .. -0.9] | +8.1 % |
| volatile through pointer (prerequisite for 4) | 5c4313d5 | dekkerMul2 ldr/str 9/6 -> 29/13 (21/12) | +0.4 % | +2.5 % [+2.0 .. +3.1] | |
| 4 byte assembly: one load + rev/rev16; calls read only their argument registers | b72162c6 | rev 0 -> 100 (186), rev16 0 -> 65 (41) | -1.2 % | -0.8 % [-1.2 .. +0.1] | +9.7 % |
| 5 constants: wzr/xzr store, add/sub/cmp and logical immediates | a4d0daec | stored zero via register 1722 -> 333 (132), constant move 24294 -> 22602 | -0.3 % | +0.1 % [-0.2 .. +0.3] | +9.9 % |

- Point 2: copies of small blocks with work in them (the loop test last in
  the latch) were tried and removed: more instructions (constants and edge
  moves are lowered once per copy).
- Points 3-5 are one pass over the records (`opt.c` `fold`, only VIR's
  functions, exact liveness): the definition goes when its register is only
  read by the next reader in the block and is dead after it.  VIR's return
  reads only x0 for integers/pointers <= 8 bytes.
- Volatile: VIR built every load/store as non-volatile; the fix costs cycles
  because VIR's floating-point loads go via x16 + `fmov`.
- Remaining per function: VdbeExec (loop test is not rotated, jump table with
  4-byte entries against LLVM's 1 byte), RecordCompareInt/String (parameters
  are copied to callee-saved x19/x20 although only sibling calls follow: the
  allocator), SerialGet (LLVM inlines sqlite3Get4byte), csel is not used at
  all (0 against 980).
- For the allocator: parameters to callee-saved before sibling calls (fixed
  on `claude/vir-regalloc-impl-f22a1d`: the across-call test goes by live
  sets).  Constants that a record fold absorbs should instead be marked in
  `vl_fix` (the mem bit) so that they never get a register: the lowering's
  business.
- Rebased on the allocation branch (eab5ebd0): one set of records, its
  `I_LDX`/`I_STX`/`I_SMADDL`/`I_UMADDL`; `I_ADDXT`/`I_SUBXT` became
  `I_ADD`/`I_SUB` with cond 8 + option (like the shift kinds).  Pairwise,
  size 100: against eab5ebd0 -6.8 % instr., -2.7 % cycles; against virpol
  +6.8 % cycles; against clang -O2 +10.3 % (virpol against clang -O2
  +3.9 %).
- After rebasing on 5ae88996 (the allocator's across-call test): the low half
  (32-bit return, int argument, through block parameters) gives no `mov wN,
  wN` (724 -> 56 in sqlite3.s), switch indexes the table with the selector
  itself (2e4ad9d8: -0.7 % instr., -0.1 % cycles); multipliers that `fmulk`
  folds get no register (`vl_fix`: -0.4 % instr., cycles within noise).
  Against virpol +5.5 % [+5.1 .. +5.7], against clang -O2 about +10 %.
  Remaining here: `x * -1 + a` becomes madd with -1 instead of sub.
- If-conversion in the builder (`vw_ifconv`, before `vw_split`): a branch
  whose arms are empty or have at most two cheap instructions without
  effects and that differ in one value at the join becomes `select`.
  sqlite3.s: csel 0 -> 984 (LLVM 980), conditional jumps 30360 -> 29372, 438
  fewer instructions statically; pairwise +0.2 % instr., cycles -0.1 % [-0.8
  .. +0.3] and -0.9 % [-1.6 .. -0.3] in two runs (within noise).  Against
  virpol +5.5 %, against clang -O2 +10.0 %.  Remaining: constants in csel
  (csinc/wzr), `and` + `subs` before csel as `tst`.
- **Review (2026-11-12) and fixes.**  Three miscompilations from this branch,
  all in the record cleanup (`opt.c`), each with a test that fails without
  the fix (a51b4106): `exts` read an add with an extended operand as a plain
  64-bit add (`vir_ext_add_facts.c`); `flags` removed a compare at a label
  that a later jump goes back to (`vir_flags_backedge.c`); `fold` moved a
  read of x16/x17 past a record that rebuilds them
  (`vir_scratch_fold.c`).  The older passes are not exposed (only
  x0-x15/x19-x28).  `returns_twice` and `setjmp_sites` timed out through VIR
  because of the `flags` bug: known failures 5 -> 3.
  **The measurements above from point 1 onward are too good**: the bug hit
  `sqlite3_str_vappendf` (a compare at a loop header was missing, the hash
  still matched).  Corrected: +2.2 % instr., +2.7 % cycles [+2.5 .. +2.7];
  against virpol **+7.9 %** [+7.1 .. +8.6].
- Volatile through VIR (8dceb271): locals, bit-fields, initializers and dead
  stores; `bench/vcc-volatile.py` 32/32 (25/32 before), not in CI yet (only
  run on macOS).
- The low-half sweep goes backwards (b096c081): 4000 chained `if` 0.57 s ->
  0.02 s.
- The review's open points, waiting for a decision: if-conversion as a pass
  over finished VIR instead of in the builder's pre-IR; 32-bit arguments and
  results in SPEC-vir.md (O1 says the builder extends, the lowering assumes
  the opposite); SPEC 15.3/15.4/1.3 against the code (switch, tail calls,
  folds).  Compile time: `fwriter` is now bounded; `tbz_relax` encodes each
  record in functions with a tbz; the builder zeroes its hash table at its
  maximum size per function (~6 % of sqlite).
- Tests: `vir_branch_bits.c`, `vir_flags_reuse.c`, `vir_layout_exits.c`,
  `vir_operand_fold.c`, `vir_byte_assembly.c`, `vir_constants.c` (.o and
  .s).
- **Floating-point loads and stores directly (2026-11-12).**  New records
  `I_LDF`/`I_STF` (rd is a v register, sz 2/3, `[rn + imm]`, also against
  FRAME): `ldr d2, [x0]` instead of `ldr x16` + `fmov`.  Used by VIR's load
  and store (not symbol or register-offset addresses yet), spill/reload,
  stack arguments and stack parameters.  The record cleanup: only the base
  register is read; a frame slot that such a record touches counts as
  escaped in the slot-value pass, and `I_STF` is a store for slot values and
  the byte assembly.  sqlite3.o: `fmov` via x16/w16 487 -> 114, `.text` -1512
  bytes; dekkerMul2 89 -> 59 instructions (clang about 44 per inlined copy).
  speedtest1 size 100, pairwise 4 rounds on a loaded machine: instructions
  -0.45 % (28.65 -> 28.52 G), cycles -0.5 .. -3.5 %.  Without VIR the output
  is byte-identical (`vcc-same.py`).  Test `vir_fp_mem.c`; the C suites
  through VIR the same three known failures (`.Lswitch.values`), vcc-abi
  (Linux 34/34, `--host` the same four), vir-test 119/119, vcc-volatile
  32/32, sqlite TCL through VIR 1 failure (interrupt2, as clang).  Remaining
  in dekkerMul2: inlining in the callers, `fmadd`, leaf frame without frame
  record, `[x0, #8]` instead of `add` + `[x1]`.
- **`fma` (2026-11-12).**  New VIR instruction 9 `fma a, b, c` (72-79 are the
  vector operations) (flags `negp`, `negc`; SPEC-vir.md), lowered to
  fmadd/fmsub/fnmsub/fnmadd.  The builder contracts like clang's
  `-ffp-contract=on`: a product that the expression itself just computed
  (the name still holds it; also negated) is added or subtracted, left
  operand first; not a product in a variable, the value of an assignment, a
  converted product, one via `?:` or a constant product.  Negated factors and
  addends become flags.  `-ffp-contract=off` turns it off
  (`cc_opts.no_fp_contract`); `#pragma STDC FP_CONTRACT` is not read, and the
  direct emitter does not contract.  Dead floating-point operations are now
  removed in the builder (status flags are not preserved, as clang without
  FENV_ACCESS).  39 test expressions against clang -O2: the same operations
  except where the value is equal anyway.  sqlite3.o: 13 of the fmadd family
  (clang 46, of which 36 in the 12 inlined dekkerMul2); dekkerMul2 59 -> 57
  instructions with clang's three fmadd; speedtest1 -0.05 % instructions
  (floating point is a small part).  Test `vir_fp_contract.c` (all forms
  fused or none; those clang does not contract stay uncontracted); the C
  suites through VIR the same three known failures, without VIR 2082/2082
  and byte-identical; vir-test 119/119, vir-exposure 56/56, virgen 101/101,
  vcc-abi, vcc-driver 212/212, sqlite TCL through VIR 1 failure (interrupt2).
  After the merge with the vectorizer (main 75bb9062): `fma` also takes
  vector types (fmla/fmls, the addend negated first for `negc`) and the
  vectorizer knows it, otherwise TSVC fell from 43 to 32 vectorized loops;
  now 43 of 120, 151/151 checksums.  Main's direct floating-point loads
  (`vl_fmem`, word) go first; `I_LDF`/`I_STF` take frame slots, volatile
  accesses, spills and stack arguments.  sqlite through VIR against main:
  304176 -> 303889 instructions statically, speedtest1 28.63 -> 28.53 G.
- **ccmp (2026-11-12).**  The builder (`vw_condmerge`, before the
  if-conversion) merges two branches against a common target like LLVM's
  FoldBranchToCommonDest: `brif c1, S, O` where S holds only an integer
  compare c2 and branches on it with O as one target becomes `and`/`or` of
  c1 and c2 (c2 inverted as needed) and one branch; repeated this gives
  chains.  Compares that alone are cbz/tbz (against 0 and -1) are left.  The
  lowering (`vl_chain`, `vl_ccmp`): icmp, then (icmp, and/or) one or more
  times, then brif or select, all read
  once and in a row, becomes cmp + ccmp/ccmn (immediate 0..31, -1..-31,
  otherwise via x17) and no truth value in a register.
  **Measured, and therefore enabled sparingly:** merging every branch pair
  (`VCC_CCMP=all`) gives on sqlite 380 ccmp (clang 552) but speedtest1 +0.4 %
  instructions (the second compare also runs when the first decided) and
  unchanged cycles (median 9.10 vs 9.09 G, 8 paired runs).  Microtest (4096
  pairs, 60,000 iterations): unpredictable data 1.45 -> 1.18 G cycles (-19
  %); first condition almost always false +1 % cycles, +2 % instructions;
  almost always true and a loop-carried value 1.98 -> 2.47 G (+25 %: the
  merged branch is if-converted to csel on the dependence chain; clang makes
  the same choice).  Without a profile it is thus a gamble.  The default is
  therefore only the cases where the if-conversion can follow (the result is
  a value: `return a == 1 && b == 2`, `x = c1 || c2 ? p : q`): fewer
  instructions and no branches.  sqlite: 19 ccmp, -18 instructions
  statically, speedtest1 -0.1 % instructions.  `VCC_CCMP=off|all` for
  measurement.  Remaining to try: merge branch pairs only when they are
  badly predicted (requires a profile), chains of three in the value case.
  Test `vir_ccmp.c` (all condition codes x and/or x 32/64 bits, immediates,
  chains, select) under clang, vcc, VIR default/all/off; the C suites through
  VIR in both modes the same three known failures; without VIR 2083/2083 and
  byte-identical; vir-test, virgen, vir-exposure, vcc-abi, vcc-volatile,
  vcc-stress; sqlite hash in all three modes, TCL through VIR 1 failure
  (interrupt2).

### zlib: the block parameter's register is voted by the predecessors (2026-11-12)

`bench/zlib/run.py` (zlib 1.4, zbench on zlib's own source tar).  Profile
against clang -O2: `longest_match` is hottest and was 5 % slower.  Cause: the
loop's usual path (the first compare misses) passes `scan` and `best_len`
unchanged to the block before the back edge, but the parameters there got
the registers from the rare path (`treg`: the first argument placed), so
each iteration did four `mov` and two extra jumps.
- **Fix** (`alloc.c`, `arg_vote`): a block parameter takes the register where
  most already placed predecessors have their argument (at least two, and
  more than in `treg`; 3-32 predecessors).  The rare path gets the moves.
- **Result:** the hot loop 16 -> 10 instructions (clang 12).  zbench against
  clang: deflate-9 1.09x -> 1.05x, total 1.10x -> 1.08x; `.text` 79896 ->
  79860.
- **Remaining** (after the next section): `slide_hash` (clang vectorizes),
  `crc32_z` (inline asm, does not go through VIR).
- Verified: vir-alloc 17/17, random 300, mutate, vir-test 119/119;
  vcc-ctests with `VCC_VIR=bl VCC_VIR_CHECK=1` 2077/2080 (the three
  failures, `.Lswitch.values`, exist before the change); inline 85/85;
  sqlite3.c through VIR with the check; the zlib tests give clang's bytes.

### zlib: a load is reused across blocks and loops (2026-11-12)

The builder's value numbering (`vw_simplify`) reused a load only within one
memory state: it was broken at every join and every store, and the address
(`ptradd`) is numbered per block, so `s->window` was reread in each
iteration of `longest_match` although it was read before the loop.
- **Fix** (`build.c`, `vw_far`): a second table holds each load per base +
  constant offset for the blocks it dominates.  A later load of the same
  bytes becomes that value when nothing in between can write them;
  "in between" is the positions in reverse postorder after the first up to
  the second, or up to the end of the outermost loop that holds the second
  but not the first (the reasoning is in the comment).  A store to the same
  base at a different offset interval does not write; another base, a call,
  `memcpy`, atomics and asm write everything.  At most 32 writers are
  examined per load.  No load is moved or added: a loop's load disappears
  only when the same load already exists in a dominating block (true LICM
  with a preheader remains).
- **zlib** (zbench against clang -O2): total 1.08x -> 1.03x, deflate-9 1.05x
  -> 1.00x, deflate-6 1.14x -> 1.09x; deflate.c 1789 -> 1553 loads, `.text`
  79860 -> 78304.
- **sqlite** (through VIR): loads 67304 -> 65311, text 1217388 -> 1209808,
  speedtest1 --size 100 CPU 2.785 s -> 2.808 s (+0.8 %, within noise; more
  values live across blocks), compilation of sqlite3.c 0.632 -> 0.643 s.
- Also fixed: `vw_simp1` computed `k + 1` with sign for the mask `INT64_MAX`
  (UBSan, nine tests in the vcc suite under `build/src-asan`).
- Test: `test/vcc/vir_load_far.c` (store after the load in the body, through
  another pointer, overlapping narrower and wider, call, inner loop, one arm,
  goto loop with entry in the middle, variable index, escaped local).
- Verified: vcc-ctests with `VCC_VIR=bl VCC_VIR_CHECK=1` 2077/2080 (the same
  three `.Lswitch.values` as before) and under ASan+UBSan (besides them two
  failures in the front end, `gcc/eeprof-1` and `gcc/pr105613`, not in VIR);
  inline 85/85; vir-test, vcc-volatile; sqlite3.c under ASan; zlib gives
  clang's bytes; sqlite speedtest1 the same verification hash.

### zlib deflate-1: field addresses, narrow stores, sub+cmp, csel with zero (2026-11-12)

Profile of level 1 against clang (samples, 60 iterations): `fill_window` 229
vs 11 (clang vectorizes `slide_hash`), `deflate_fast` 596 vs 422,
`longest_match` 838 vs 722, `compress_block` 389 vs 322.  Four general
shortcomings in the code fixed:
- **Field address per access** (`build.c`, `vw_ownaddr`): the numbering gives
  a block's accesses to the same field a shared `ptradd`, and the lowering
  folds only a `ptradd` with one user directly after.  Each `load`/`store`
  with base + encodable constant (-256..255, or a multiple of the width up to
  4095) now gets its own copy just before it: `add x1, x19, #160; ldr w2,
  [x1]; ...; str w2, [x1]` becomes `ldr w2, [x19, #160]; ...; str w2, [x19,
  #160]`.
- **Narrow store** (`vw_simp1`): `store` reads only the low bytes, so the
  value's `ext` (at least that wide) or `trunc` is skipped (`ubfm` before
  `strh`/`strb`).  Not for stores to promoted slots: their operands do not
  count.
- **sub + cmp** (`opt.c`, `flags`): a `sub` directly before a `cmp` of the
  same operands becomes `subs` and the compare goes.
- **csel with zero** (`a64.c`): a zero arm is read from `wzr` instead of
  being materialized.
- **Result:** the `slide_hash` loop 10 -> 7 instructions; deflate.c 5586 ->
  5219 instructions; zlib `.text` 78304 -> 74436.  zbench against clang -O2:
  deflate-1 1.36x -> 1.19x, deflate-6 1.09x -> 1.02x, total 1.03x -> 1.02x.
  sqlite through VIR: 292365 -> 287144 instructions, text 1209808 ->
  1188920, speedtest1 CPU 2.742 -> 2.731 s (2.768 before today's three
  changes).
- **Remaining on level 1** (requires a decision): `slide_hash` needs
  vectorization (clang: NEON `uqsub`); `deflate_fast`/`compress_block`
  reread `s->` fields after each store through `s->head`/`s->prev` (`ush`),
  which clang avoids with type-based alias, which VIR by the decision of
  2026-11-11 does not have without a flag.  Without a decision: store -> load
  across stores to the same base at another offset (`str w2, [x0, #8]; str
  w1, [x0, #12]; ldrsw x1, [x0, #8]`).
- Test: `test/vcc/vir_field_access.c`.  Verified: vcc-ctests `VCC_VIR=bl
  VCC_VIR_CHECK=1` 2079/2082 (the same three), with the verifier (`blv`) the
  vcc suite, inline 85/85, direct emitter 2082/2082, ASan+UBSan as before,
  vir-test, vcc-volatile, sqlite3.c with the check and the same verification
  hash, zlib gives clang's bytes.

### Compile time through VIR (2026-11-12): sqlite3.c

Branch `claude/vir-sqlite-optimization-acaf18`.  Measured on sqlite3.c `-O2`
(the amalgamation, 1438 functions through VIR), instructions for the whole
`vpp` process (the machine is shared: times are the minimum of nine runs).

| | Instructions | Emit, 1 thread | Emit, 8 threads |
|---|---|---|---|
| direct emitter | 1.29 G | 57 ms | |
| VIR before | 4.84 G | 438-540 ms | 90 ms |
| VIR now | 3.75 G | 409 ms | 62 ms |
| clang -O2 (the whole compilation) | | about 8-10 s | |

- **Done (ac6639cd), same object byte for byte:** `vw_promote` has field
  lists per slot (was quadratic: each access searched all the function's
  fields, each store went through them again); the (block, variable) hash
  has generations instead of clearing per function and starts over at 1024
  entries per function (the review's point "clearing at the largest size",
  ~6 % of sqlite); the label table is cleared only as far as it was used;
  `vw_local` looks up (symbol, instance) in a hash; `vir_oit_init` reads the
  field mask from a table.
- **Threads:** `VCC_THREADS=8` gives 62 ms emit (6.6 times vs one thread),
  which is on a level with the direct emitter on one thread.  The default is
  still one thread (the kernel build parallelizes over TUs); a decision is
  needed on whether a single large TU should get more.
- **Bug the threads showed (b236ef95):** the allocator's `dmark` was read for
  values not yet used in the function, thus an earlier function's stamp: with
  several threads sqlite3.o differed between runs (16 functions).  Zeroed
  now at each definition; 26 functions change even on one thread.  The emit
  threads got Darwin's default stack (512 KB): `test/vcc/limits.c` gave no
  object on 8 threads; they now get the creating thread's size.
- **Open, not VIR:** `test/vcc/deep_expr.c` and `deep_mixed.c` give different
  objects between runs with `VCC_THREADS=8` also on the direct path (without
  `VCC_VIR`).  Not investigated.
- **Remaining in the profile (flat, no entry over 6 %):** `vw_hslot` 6 %
  (Braun's lookup per (block, variable); cache misses in VdbeExec),
  `vw_promote` 4.5 %, `vw_find` 4 %, `vir_oit_next`/`init` 5 %, `vw_simplify`
  + `vw_cse_blk` 4 %, the allocator (`instr`, `sweep5`, `conform`, `enter`)
  about 8 %, the record cleanup (`opt_function`).  Next steps with measurable
  effect: the operand iterator inline in `vir.h` (called across the TU
  boundary), trivial phis removed directly (the review's first
  recommendation), `vir_canon` without double dominators.

Code quality on the same branch (519b6905): floating-point values are loaded
and stored without a general register also for volatile, frame slots and
spills (sqlite's `dekkerMul2` was 2.9 % of speedtest1);
`__builtin___memcpy_chk` etc. with an object size that cannot be exceeded
becomes the ordinary call also when the length is not constant (530 `_chk`
calls in sqlite).  speedtest1 size 100: 8.76 -> 8.56 G cycles (clang -O2
7.83 G), same verification hash.  Largest known remainder against clang:
address-taken locals in loops without calls (`balance`: `b.nCell` is loaded
and stored each iteration; requires alias analysis on type or origin, a
decision for SPEC-vir.md).

### What the allocator's quality is worth: LLVM's allocators against each other (2026-11-12)

The measurement that "Measured in V4" below promised.  sqlite3.c,
speedtest1 `--size 100 --memdb`, Homebrew clang 23.1.4 `-O2` with `-mllvm
-regalloc=X`, vcc on 75bb9062 (`VCC_VIR=bl`), virpol is the binary from
12:36 the same day (older inlining).  Five alternating rounds, `/usr/bin/time
-l`; the machine had load 10, so the cycles are the median of paired ratios
with [min .. max], and only differences over about 2 % say anything.  The
same verification hash in all seven.

| | Instr. | Cycles vs greedy | Static: total | `mov` r,r | sp/fp loads | sp/fp stores |
|---|---|---|---|---|---|---|
| clang greedy | 23.70 G | 0 | 239 k | 21.3 k | 13.0 k | 10.2 k |
| clang pbqp | +3.5 % | +2.1 % [-0.6 .. +10.7] | 242 k | 22.7 k | 14.7 k | 10.4 k |
| clang basic | +3.7 % | +7.0 % [+4.7 .. +19.3] | 242 k | 17.4 k | 19.6 k | 10.7 k |
| clang fast | +44.1 % | +72.4 % | 325 k | 3.1 k | 83.3 k | 44.8 k |
| virpol | +3.4 % | +6.0 % [+2.2 .. +14.1] (best of five +3.9 %) | 242 k | 21.1 k | 12.6 k | 10.0 k |
| VIR | +15.3 % | +12.6 % [+10.8 .. +23.9] | 298 k | 35.1 k | 22.0 k | 17.9 k |
| vcc without VIR | +60.9 % | +50.7 % | 234 k | 24.0 k | 28.5 k | 21.9 k |

- **VIR against virpol: +8.4 % cycles [+6.3 .. +8.6], +11.5 % instructions.**
  VIR against clang basic: +4.1 % [+3.7 .. +7.4].
- **Greedy against basic is worth about 7 % here**, as much as VIR's whole
  gap against virpol.  Basic has fewer moves than greedy (17.4 k vs 21.3 k)
  but 50 % more stack loads: what costs is reloads on hot paths (splitting
  of live ranges), not the number of `mov`.  Fast shows the floor without
  allocation.
- **Static excess VIR against virpol, 56.5 k instructions:** moves 14.0 k,
  stack loads 9.3 k, stack stores 8.0 k (together 55 %: allocation and
  frames), jumps 4.9 k, constants 3.3 k, other 17 k (selection, copied
  blocks).
- **Not measured:** the allocator's share of the 8.4 percent directly
  (requires the same functions through both allocators; the recipe for
  virpol is not in the tree), and nothing dynamic: the static figures do not
  say which moves and loads run.  Estimate: virpol with basic would lie at
  about VIR's level (8.11 G x 1.07-1.08 vs 8.79 G).
- **Conclusion for the work order:** reloads in loops before moves.  PBQP's
  +2 % shows that an allocator without splitting can come close to greedy
  with a good cost model.

### The allocator: loop entry, arguments as a parallel move, callee-saved directly (2026-11-12)

The first step after the measurement above, after merging with main (d0cbdcc7).
Measured in **instructions** (`/usr/bin/time -l`, speedtest1 `--size 100 --memdb`):
they are stable to about 0.05 % even with load 10-20 on the machine, which
cycles are not.  Base after the merge: 26.18 G (virpol 24.49 G, clang 23.70 G),
8.5-8.6 G cycles (virpol 8.24 G, clang 7.86 G).

- **What actually runs** (`VCC_VL_PROBE=MASK` in `codegen/vir/a64.c`: a `nop`
  after each edit of a kind; the difference in instructions is the number of
  executions, an upper bound since the `nop` also stops the post-folds):

  | Kind | Before instruction | On edge | Sum | Share of 26.1 G |
  |---|---|---|---|---|
  | move | 1.23 G | 0.60 G | 1.83 G | 7.0 % |
  | rematerialization | 1.00 G | 0.55 G | 1.55 G | 5.9 % |
  | reload | 0.34 G | 0.28 G | 0.62 G | 2.4 % |
  | store to slot | 0.16 G | 0.34 G | 0.50 G | 1.9 % |

  Moves and constants thus run three times as often as reloads and stores.
  The conclusion in the previous section ("reloads in loops before moves")
  held for LLVM's basic against greedy and does not hold for our allocator on
  this load.  The moves before an instruction by cause: copy of a live value
  to a call's argument register 0.73 G, parameter or result to callee-saved
  0.29 G, live value moved out of an argument register before a call 0.30 G,
  other 0.15 G.  The first two follow from the calling convention; clang has
  them too.
- **Done, three rules in `vir/alloc.c` (SPEC 15.6-15.7 updated):**
  1. *Loop entry* (`preload`): a value that reaches the loop header in its slot
     and is used in the loop gets a register in the header's entry state, so
     that the edge into the loop loads it once.  Only while registers are left
     over (maxp + 1); in a loop with calls only callee-saved.  Each loop now
     has `lcall` (a call outside cold blocks).
  2. *Fixed operands as a parallel move:* an argument that moves from one
     argument register to the next is copied before its register is written.
     The reduced case (`add4(v, 96, base, n, 0, zAff, n)`): 8 -> 6
     instructions.
  3. *Callee-saved directly:* a value that lives across a call takes a free
     callee-saved register also outside loops and when the function has not
     used any yet.  Before, it ended up in a caller-saved register and was
     moved at the call.
- **Result, sqlite:**

  | | Instr. | Static total | `mov` r,r | sp/fp loads | sp/fp stores |
  |---|---|---|---|---|---|
  | base | 26.186 G | 289 168 | 34 535 | 22 379 | 18 100 |
  | 1 | -0.18 % | 289 060 | 34 486 | 22 318 | 18 111 |
  | 1 + 2 | -0.23 % | 288 572 | 34 057 | 22 260 | 18 110 |
  | 1 + 2 + 3 | **-0.42 %** (26.076 G) | 285 866 | 31 591 | 22 269 | 17 996 |

  Cycles: four paired runs within the noise (load 23).  Frames unchanged, 1138.
- **Tried and dropped, all measured in instructions:**
  - *Value from the entry state at a join* when another predecessor has it in
    its slot and the block does not read it: statically -3.4 k stack loads
    (-15 %), dynamically **+0.6 %**.  The edge reloads (8.6 k statically, of
    which 7.0 k for values the target block does not read) thus lie mostly on
    cold paths; the static numbers in the previous section overestimate them.
    The safe variant (the block calls before the use) gave -264 loads and
    nothing measurable.
  - *Constants in registers across loops without calls:* +838 instructions
    statically, +0.07 % dynamically (more rematerializations on the edges).
  - Sampling (`sample`) per instruction kind: the sample lands after
    mispredicted branches and says nothing about the instruction mix.
- **Tests:** `vir-alloc` 19/19 (new `loop-preload.vir`, `args-parallel.vir`;
  `irr-head.vir` and `irreducible.vir` reconfirmed: the reload moved to the
  edge), random 600 units (seed 1-300, 1000-1299), `mutate` 3243 of 3355;
  `vir-test` 126/126; the C suites 2091 without VIR (vcc 288/288), through VIR
  with the checker 2088/2091 (the three known: jump_table, switch_dispatch,
  switch_values); inline through VIR 85/85; new `vcc/vir_loop_entry.c`; sqlite
  through VIR with the checker, hash unchanged.  Not run: `vbench.py gate`,
  the TCL suite, PostgreSQL, zlib.
- **Remaining, in order of what the probe shows:** moves on edges (0.60 G:
  block parameters and back edges), the move-out before a call (0.30 G, a
  value that is not `xcall` according to the live sets but sits in an argument
  register), rematerialization on edges (0.55 G: constants as block
  arguments).  The gap to virpol in instructions (+6.5 %) is larger than
  everything the allocator can take of this; loops that are not rotated (one
  `b` per iteration) belong to the lowering.

### Frame in functions with a jump table, and where the edge moves run (2026-11-12)

- **Against clang -O2 after the previous section** (six paired runs, load
  11-14): cycles **1.095x** [1.083 .. 1.125], instructions 1.100x; virpol
  against clang 1.049x [1.034 .. 1.086].
- **Frame:** `opt_regs` (arm64, `opt.c`) gave `OPF_ALL` for `I_JT`, so a
  function with a jump table kept frame records and all callee-saved registers
  even though it did not need them.  `I_JT` reads its index and writes
  x16/x17; the cleanup passes still see it as `OP_ALL`.
  vdbeRecordCompareInt (only sibling calls) and two more in sqlite lose
  `stp/mov/ldp`: 26.10 G -> 25.97 G instructions (**-0.5 %**, more than the
  three allocation rules together), statically 1138 -> 1135 frames.  Applies
  to the direct emitter too.  Test: `vcc/vir_switch_leaf.c`; vcc-cfi 1582/1582
  direct and through VIR.
- **Edge moves by kind** (nop probe, G runs): argument that dies on the edge
  into a block parameter in another register 0.38 forward and 0.20 on a back
  edge; ordinary values 0.16 forward and 0.06 on a back edge; constant as a
  block argument 0.53 (forward).  Per function they are spread out: largest
  sqlite3VdbeExec 0.09 and balance 0.06 of the twelve hottest.  No single rule
  takes them; nothing changed.
- **Frame against clang in the hot functions after the fix:** only
  sqlite3PagerWrite and pcache1Fetch have a frame with us and not with clang
  (shrink-wrapping).

### Loop rotation in the lowering (2026-11-12)

- **Done (`codegen/vir/a64.c`, `vl_order`):** the blocks are lowered in an order
  of their own (`L->ord`; `vl_next` and `vl_body` were the only places that
  assumed VIR's order).  A loop whose header is a test (`brif` after at most
  seven simple instructions, no calls) gets the header after its last block
  when that block ends with `br` to the header: the latch falls into the test
  and the test's jump goes back to the body, one jump per iteration instead of
  two.  The edges into the loop get a copy of the test (`vl_goto`), as LLVM's
  rotation with a guard.  `VCC_VIR_ROT=0` gives VIR's order.
- **Two variants measured** (sqlite speedtest1, instructions):
  reordering only, with a jump to the test on the way in, **+0.07 %**
  (sqlite's loops are short: the jump in costs more than the iterations
  save); with the copy **-0.44 %** (25.94 G -> 25.83 G).  Statically +1.5 %
  instructions (285 802 -> 289 983), `b` 16 653 -> 15 388.  The copy of a
  block with work in it, which was tried before at the latch and dropped,
  thus pays off here, where it replaces a jump that otherwise runs once per
  loop.
- **Time:** sqlite paired (six runs, load 10-12): -0.5 % cycles
  [-1.0 .. +3.5] against the build before; against clang -O2 **1.083x**
  [1.077 .. 1.094] cycles, 1.090x instructions.  TSVC through VIR: 6.42 G ->
  6.31 G cycles (-1.7 %), 151/151 checksums.
- **Tests:** new `vcc/vir_loop_rotate.c` (0, 1 and many iterations, nested,
  continue/break, loop reached from two branches and with `goto` past the
  test); the C suites 2093/2093 direct and 2090/2093 through VIR with the
  checker (the three known), inline 85/85, vcc-cfi through VIR 1582/1582,
  volatile 32/32, vcc-abi `--host` through VIR 4 failures (the known four as
  above; not compared against a build without the change), sqlite same hash.
- **Seen on the way, not fixed:** a leaf function with vector instructions
  gets a frame record (the vector entries are `I_RAW`, hence `OP_ALL` for
  `opt_regs`).

### What runs, against clang, per instruction kind; copied join tests (2026-11-12)

- **Tool:** `bench/sqlite/mix.py`: a `nop` in front of each instruction of a
  kind in the compiler's `.s`, and the difference in instructions is the
  number of executions.  Works on clang's `.s` too, so the mix can be compared
  exactly (the sum of the differences below is 2.16 G against a measured gap
  of 2.13 G).
- **sqlite speedtest1 `--size 100`, G instructions run** (VIR after the loop
  rotation 25.82 G, clang -O2 23.69 G):

  | Kind | VIR | clang | Difference |
  |---|---|---|---|
  | loads (not sp/fp) | 4.42 | 3.89 | **+0.52** |
  | conditional jumps | 2.87 | 2.51 | **+0.36** |
  | `mov` r,r | 1.53 | 1.18 | **+0.35** |
  | `b` | 0.91 | 0.62 | **+0.29** |
  | add/sub | 2.77 | 2.59 | +0.18 |
  | cmp/tst | 1.70 | 1.58 | +0.12 |
  | sp/fp loads | 1.28 | 1.15 | +0.12 |
  | sp/fp stores | 1.12 | 1.01 | +0.12 |
  | stores (not sp/fp) | 1.12 | 1.00 | +0.12 |
  | call + ret | 0.65 | 0.57 | +0.08 |
  | constants (mov #, adrp) | 0.99 | 1.11 | -0.13 |
  | csel family | 0.10 | 0.19 | -0.09 |

  The stack traffic is thus nearly clang's (+0.24 G of 2.13); the largest is
  ordinary loads (no loads hoisted out of loops, fewer reused across blocks,
  inlining), then jumps and moves.  Per function, sqlite3BtreeIndexMoveto
  stands out (`b` 0.08 against 0.01, `mov` 0.13 against 0.07) and
  sqlite3VdbeExec in `b` (0.39 against 0.27); VdbeExec has fewer `mov` than
  clang.
- **Done (`vl_small` in `codegen/vir/a64.c`):** a forward jump to a block that
  only tests (`brif` with at most one instruction before it, e.g. `if (c<0)`
  after three branches that compute `c`) is replaced by a copy of the block,
  as LLVM's tail duplication at its default size.  The path to the test's jump
  side then costs one jump instead of two.
- **Measured:** 25.84 G -> 25.76 G instructions (**-0.31 %**), cycles better
  in three paired runs of three (-0.5 to -1.8 %), code +2.7 % (289 983 ->
  297 728).  Larger blocks: 3 instructions -0.44 % for +3.9 %, 4: -0.52 % for
  +5.1 %, 6: -0.59 % for +6.5 %; 2 chosen.  Only copies that fall through
  afterwards: no difference at all (the gain is on the jump side).
- **Tests:** new `vcc/vir_join_dup.c`; the C suites through VIR with the
  checker 2091/2094 (the three known), vcc 291/291 direct, inline 85/85,
  vcc-cfi through VIR 1582/1582, volatile 32/32, TSVC 151/151 (6.32 G cycles),
  sqlite same hash.

### Frames against clang per function: sibling calls returning pointers, FP leaves (2026-11-12)

- **Measured exactly** (a counter in front of each `stp x29, x30` in the `.s`,
  summed at exit): VIR set up 172.9 M frames on speedtest1 `--size 100`, clang
  134.0 M.  Largest: pcache1Fetch 6.3 M, sqlite3_value_text and
  sqlite3ValueText 5.8 M each, sqlite3PagerWrite 4.1 M, dekkerMul2 3.7 M,
  applyAffinity 3.5 M, sqlite3DbMallocRawNN 3.4 M, sqlite3_free 3.2 M (clang 0
  in all).
- **Most of it was not shrink-wrapping but missed sibling calls:** a function
  that returns a pointer never got any.  `ret` names the call's value under
  its `i2p` (the lowering reads through `i2p`/`p2i`), and `vl_sibling` compared
  with the value on top.  Now the values are compared without such
  instructions (`vl_under`).  sqlite: 25.755 G -> 25.670 G instructions
  (**-0.34 %**), 23 functions without a frame, 172.9 M -> 149.0 M frames.
- **FP and vector instructions as `I_WORD`** gave `OPF_ALL` in `opt_regs` and
  thereby a frame record in every leaf function with floating point
  (sqlite3_value_int64, dekkerMul2) or vectors (TSVC).  In VIR's functions the
  word's register fields are now decoded as general registers (a superset;
  branches, exceptions and field 31 outside the FP class still give
  `OPF_ALL`).  -0.06 %, 12 functions; TSVC 6.32 G -> 6.29 G cycles.
- **What remains that is real shrink-wrapping** (one callee-saved register and
  a real call on one path, early returns or sibling calls on the others):
  sqlite3PagerWrite 4.1 M, applyAffinity 3.5 M, sqlite3_free 3.2 M,
  sqlite3DbMallocRaw 1.9 M, sqlite3VdbeMemStringify 1.3 M, sqlite3Step 1.1 M:
  about 17 M frames at about 8 instructions each, 0.5 %.
- **Tests:** new `vcc/vir_tail_ptr.c`; the C suites through VIR with the
  checker (the three known failures), inline 85/85, vcc-cfi through VIR
  1582/1582, vcc-abi `--host` 4 failures both with and without VIR (so not
  from here), TSVC 151/151, sqlite same hash.

### Shrink-wrapping (2026-11-12)

- **Done, three parts (SPEC 15.12):**
  1. *The allocator* (`save_point` in `vir/alloc.c`, on request through
     `vir_aopt.shrink`): the save point S is the block that dominates all
     blocks that need the frame (calls that are not sibling calls,
     memcpy/memset, asm, `va_start`, the address of a slot, `VB_ADDR`), lies
     outside all loops and dominates everything reached from it; blocks that
     only return are exempt.  In front of S no callee-saved registers are
     given when that can be avoided, and a parameter is not moved there at its
     definition.  At S the values that live across calls are moved to
     callee-saved (edits before S's first instruction).
     `vir_alloc_out.spoint`.
  2. *The lowering* (`codegen/vir/a64.c`): the blocks in front of S are placed
     first, S gets the marker `NOP_PROLOG` after its label, returns and sibling
     calls in front of S are marked (`I_TAIL.sh`).  Code behind S that goes to
     a return block in front of S gets a copy of its own (`vl_lbl`,
     `VLT.lbf`).  The lowering says before the allocation which calls become
     sibling calls (`vl_sibcand`).
  3. *The frame* (`finish_function`, `opt_noframe`): the prologue is placed at
     the marker when the entries in front of it call nothing, touch no frame
     slot and neither read nor write x19-x30; otherwise at the top as before
     and the marks are removed.  Not with unwind tables (Linux
     `-fasynchronous-unwind-tables`), `-g`, VLA/alloca or an over-aligned
     frame.  `VCC_VIR_SHRINK=0` turns it off.
- **Measured, sqlite speedtest1 `--size 100`:** 25.645 G -> 25.514 G
  instructions (**-0.51 %**); cycles within the noise in three paired runs.
  Statically the same number of instructions (297 581 -> 297 580).
- **Bug on the way:** a block with only `br` was skipped by the lowering
  (`vl_lonebr`) also when it was the save point and carried the moves to
  callee-saved: sqlite3WalkSelect read the parameters from registers they had
  never been moved to.  The allocation checker did not see it (the allocation
  was right); a block with edits is no longer skipped.
- **Tests:** new `vcc/vir_shrink_wrap.c` (run against clang on macOS, where
  shrink-wrapping is on); the vcc suite 293/293 direct and 290/293 through VIR
  with the checker (the three known), vir-alloc 19/19, vir-test 126/126,
  sqlite through VIR with the checker, same hash.  **Not run after this
  change:** the whole C suite (the gcc/tcc parts), inline, vcc-cfi, vcc-abi,
  TSVC, random allocations; the run was interrupted.
- **In total on the branch**, instructions against clang -O2 (23.70 G): after
  the merge with main 1.105x, now 1.077x (25.51 G).

### What remains of the gap to clang, per instruction kind (2026-11-12, after shrink-wrapping)

`bench/sqlite/mix.py`, the classes side by side (one minute; each number about
0.03 G too high, the same for both).  VIR 25.55 G, clang -O2 23.72 G: gap
**1.84 G (7.7 %)**.  G instructions run:

| Kind | VIR | clang | Difference | At the previous measurement |
|---|---|---|---|---|
| loads (not sp/fp) | 4.45 | 3.99 | **+0.46** | +0.52 |
| `mov` r,r | 1.59 | 1.21 | **+0.38** | +0.35 |
| conditional jumps | 2.93 | 2.63 | **+0.30** | +0.36 |
| `b` | 0.90 | 0.64 | **+0.26** | +0.29 |
| add/sub | 2.77 | 2.58 | +0.19 | +0.18 |
| cmp/tst | 1.74 | 1.61 | +0.13 | +0.12 |
| stores (not sp/fp) | 1.15 | 1.04 | +0.12 | +0.12 |
| sp/fp stores | 1.13 | 1.04 | +0.09 | +0.12 |
| sp/fp loads | 1.27 | 1.19 | +0.08 | +0.12 |
| call + ret | 0.72 | 0.61 | +0.11 | +0.08 |
| constants (mov #, adrp) | 1.04 | 1.23 | -0.19 | -0.13 |
| csel family | 0.14 | 0.20 | -0.06 | -0.09 |

The sum of the differences is 1.87 G.  Frames (`stp x29, x30`): VIR 0.11 G,
clang 0.16 G: we now set up fewer frames than clang.  The stack traffic is
+0.17 G (was +0.24).  What remains, in order: ordinary loads (the builder: no
loads hoisted out of loops, fewer reused across blocks, inlining), `mov` (the
allocator: block parameters and argument copies), conditional jumps (inlining,
fewer `csel`), `b` (block placement).

### Moves: three rules tried without measurable gain; main after #125 (2026-11-12)

Branch `claude/vir-moves` on main after #126.  sqlite speedtest1 `--size 100`,
instructions; the noise between runs is about +-0.005 G.

- **Main after #125 is 25.71 G, the branch before was 25.52 G.**  #124 changes
  nothing (26.178 against 26.181 on d0cbdcc7); #125 gives +0.19 G (+0.7 %):
  the if-conversion in the builder now makes up to eight `select`s per join
  instead of one, so both arms are counted more often.  The cycles are
  unchanged (six paired runs: -0.1 % [-0.5 .. +0.9]).  The vectorizer is not
  it (`VCC_VIR_VEC=0`: 25.70 G).  Comparisons in instructions against clang
  thus start from 1.084x now, not 1.077x.
- **Tried against `mov` (+0.38 G against clang), none of it kept:**
  1. A block parameter that is only returned takes the return register, and
     its argument is hinted there: -0.003 G, statically +85 instructions.  The
     case that motivated it (sqlite3DbMallocRawNN: `mov x2, x0; mov x0, x2`)
     was already solved by the sibling calls.
  2. The copy in the argument register is kept when a value is moved to
     callee-saved at the save point: -0.006 G.
  3. A register reservation: the register for a parameter whose argument the
     block defines further down is held free until then: -88 `mov`
     statically, dynamically nothing.
- **The edge moves per kind of value** (nop probe, G runs; about 0.52 in
  total): forward edge: argument defined in the predecessor 0.14 (of which
  0.09 where the parameter's register was known but occupied, 0.06 where the
  argument set the tentative register and the parameter ended up elsewhere
  anyway), argument that is a block parameter 0.06, defined earlier 0.06, from
  a call or parameter 0.05, ordinary value 0.09; back edge: argument defined
  before the latch 0.10, ordinary value 0.05.  "Occupied" is thus a value that
  was already there at the start of the block (typically the variable's old
  value, still in use), not one that took the register in the meantime: no
  hint solves it.
- **Where the rest of the `mov` gap probably lies:** the calls.  We run 0.40 G
  calls against clang's 0.34 G, and each call costs argument copies of live
  values (0.73 G when measured) and moves to callee-saved: 0.06 G more calls
  at about three moves each is roughly half of the gap.  It follows the
  inlining.

### The interpreter's `break` block is copied into the cases (2026-11-12)

- **Cause of `b` in sqlite3VdbeExec (0.39 G against clang's 0.27 G):** 201 of
  the function's 1181 `b` went to a block with `add x25, x25, #24; b`
  (`pOp++` and the jump back to the dispatch).  Each opcode thus ran two
  unconditional jumps, clang one.
- **Done (`vl_small`):** a forward jump to a block with at most one
  instruction before its `br` is replaced by a copy of the block, which
  already applied to blocks ending in `brif`.
- **Measured, sqlite speedtest1 `--size 100`:** 25.720 G -> 25.574 G
  instructions (**-0.57 %**), code +1.05 % (295 592 -> 298 695); cycles
  -1.4 %, -0.5 %, +0.1 % in three paired runs.  Against clang 1.078x in
  instructions.
- **Tests:** new `vcc/vir_break_dup.c`; the C suites through VIR with the checker
  2102/2102 (the three switch failures are gone on main), inline 85/85, vcc
  299/299 direct, TSVC 151/151, sqlite same hash.
- **TSVC on main is 7.37 G cycles, was 6.29 G before #125** (77 vectorized
  loops against 43).  The same number with and without this change, with
  `VCC_VIR_SHRINK=0` and with `VCC_VIR_ROT=0`: it does not come from here and
  has not been investigated.

### TSVC after #125: search loops (2026-11-12)

TSVC through VIR went from 6.26 G cycles (de4e5a65) to 7.37 G on main.  Per
loop (`vcc-tsvc.py -v` with both compilers) it was all in five loops: s314 and
s316 (largest and smallest value: 0.026 and 0.019 s -> 0.172 and 0.171), s315,
s3110 and s13110 (largest value with index: 0.005 -> 0.035).  Two causes, both
from #125:

- **An ordered reduction alone was vectorized:** the vector loop read four
  elements and then took them out one at a time with compare and select in
  order, that is, the scalar loop with a vector load in front.  A loop without
  a store and without a packed sum is no longer vectorized on arm64 (`vec.inc`;
  x86-64 left as it was written, not measured here).  7.37 G -> 6.60 G.
- **The if-conversion** now makes `select` also when the arm only reloads what
  the condition read (`vw_armreuse`) and for several values per join.  In a
  search loop this becomes a chain where each iteration waits for the previous
  one, where the jump was almost never taken before.  Values that each either
  keep a block parameter or replace it now remain a jump (`vw_ifconv`).
  6.60 G -> 6.21 G.  sqlite speedtest1: 25.56 G -> 25.49 G instructions,
  cycles unchanged.
- With only several values excepted, s314/s316 ended up at clang's level
  (0.053 s); clang converts them too.  The jump is faster on TSVC's data.
- **Tests:** new `vcc/vir_search_loop.c` (equal values, -0, NaN); the C suites
  through VIR with the checker 2103/2103, inline 85/85, vcc 300/300 direct,
  TSVC 151/151, sqlite same hash.
- **x86-64 (2026-11-13):** the rule in `vw_ifconv` first applied to all
  targets and broke `bench/vcc-vir-x86.py` in CI (the tsan job runs on
  x86-64): the test requires that maxval, maxindex and lastnegative are
  vectorized there, as ordered reductions of exactly the `select`s the rule
  removed.  The rule no longer applies to x86-64, as the one in `vec.inc`.
  Whether the loops are faster that way on x86-64 is not measured.

### Basis for V4 (2026-11-11): register allocation, no code

- `RESEARCH-regalloc.md`: what GCC (IRA, LRA) and LLVM (Greedy, Fast, PBQP,
  MLGO) do and where they are weak; SSA-based allocation (chordal graphs,
  Braun-Hack spilling, hints instead of coalescing); linear scan and its
  successors (C1, Go, V8, JavaScriptCore, regalloc2, TPDE, LuaJIT, QBE);
  optimal and learned methods; comparison table.  Source per claim,
  unconfirmed points marked.
- **Answer to the question "can we do better than clang and gcc?":** on
  compile time yes, by a wide margin (two linear sweeps instead of interval
  trees and a priority queue); on code quality no, in general.  Better code
  only at a few specific places: asm alternatives (`"rm"`) are decided in the
  allocator, the choice of callee-saved register is made with the calls'
  positions known, and the failure classes around subregisters and
  two-address rewriting before the allocation do not exist.  LLVM and GCC are
  ahead in splitting driven by block frequency, fifteen years of special cases
  and shrink-wrapping.
- **Recommendation (SPEC-vir.md draft 0.4, section 15):** not linear scan over
  intervals.  Sweep 5 (backward, together with `dce`) builds a stack with the
  next use per operand and live-in lists per block; sweep 6 (forward) assigns
  registers greedily, follows hints and evicts the value with the longest
  distance to its next use.  A value with a stack slot is stored once, after
  its definition.  The number of sweeps is unchanged (7).  Fallback if the
  quality gate in V7 fails on the allocation: a backtracking allocator in
  regalloc2's style behind the same interface.
- **Uncertain:** no measurements of our own (all numbers are others'); quality
  under register pressure against today's Greedy is unpublished; the rule for
  irreducible graphs is derived here and must be covered by `vir_alloc_check`
  and `fuzz-vir`; the budget of 60 ns per VIR instruction for sweeps 5 and 6
  is a proposal.
- **Measured in V4:** `vbench.py ab emit` with `VCC_VIR=1`, static
  instructions against today's `-O2`, clang and gcc, the counters in
  `VCC_VIR_STATS`, and the same set of TUs through clang with `-mllvm
  -regalloc=basic` to see how much the allocator's quality is worth at all.

### V1 verifier: safe and linear slot exposure

The earlier analysis stopped after 1 000 full sweeps and could then accept an
incorrect `exposure` declaration. A valid dominator-ordered example with
1 003 backward-pointing block arguments stores a slot address but was accepted
without `exposed`; five links were correctly rejected. The analysis now goes
backward from exposing uses via `ptradd`, `select` and block arguments once for
the whole function. The work is linear in values, operand uses and slots; a
checked allocation holds temporary edges and queue. If the memory or the
indexes cannot be represented, the verification fails, never with an unproven
acceptance. Functions without `exposure` or slot addresses build no such graph.

56 generated cases cover long exposed and unexposed chains, cycles, several
slots and addends as well as deterministic graphs; the original verifier fails
the long regression case. Existing VIR tests 118/118 and the new 56/56 pass in
unity, separate-file build and ASan/UBSan. A further 205 small graphs gave
exactly the same verifier result as the base. The VIR tests now also run in the
arm64 and ASan premerge jobs. The local full ASan premerge passes (an existing
vccd peer check lacks passwordless sudo).

Seven alternating `perf stat` rounds for the whole VIR test corpus, including
the Python driver and 118 child processes, gave +0.006 % user instructions,
+0.46 % user cycles and +0.15 % task-clock; short samples on a shared host show
no certain general time change. On synthetic unexposed chains with 5, 64 and
1 003 links, `virtest` user instructions decreased 1.6 %, 35.8 % and 90.4 %;
with 5, 64 and 1 000 slots they decreased 0.9 %, 28.3 % and 87.6 %. These are
whole tool processes and not `-O2` compilation. Provenance is in
`work/opt-profile/vir-exposure-backward/`. The change is linked only into
virtest: 32 of 34 common objects are byte-identical to the base; `vir.o`
changes and `ppdriver.o` contains a worktree-specific include path. Eight
targeted standard outputs are identical. `vbench.py check --quick` could not be
completed here: the worktree's `lex-files.txt` is missing.

A separate earlier bug on the base: an empty VIR block body gives UBSan in
`parse.c:1839` (zero-length `memcpy` with a null source). Mutation seed 42 gave
998/1000 without sanitizer errors; both hits have the same base bug. The
smallest input is in `work/opt-pr-tmp/vir-empty-block-ubsan.vir`. The parser
bug is not part of the exposure change; V1's sanitizer/fuzz gate is therefore
still open.

### ffmpeg as a test project (2026-11-12)

`bench/ffmpeg/run.py` (branch `claude/rust-compilation-speed-research-ff36c4`):
ffmpeg n8.1.3, macOS arm64, `--disable-asm --disable-autodetect`, `-O3`, so
that the DSP code's C variants are what runs.  All trees are configured by
clang (the same `config.h`); vcc compiles all 2019 C files, clang links.

- **Correctness:** no files fell back to clang.  FATE without samples: 2740
  of 2740 for clang, the direct emitter and VIR.  One bug found and fixed
  (b44a3918): variadic arguments to library builtins did not get the default
  promotion (`float` to `double`), 23 FATE failures.  With `-ffp-contract=off`
  all 21 workloads in `speed` give clang's bytes from both paths; with
  contraction, four floating-point workloads differ (other fmadd choices).
  FATE with samples (the decoders against real streams) has not been run.
- **Compile time** (2022 files, `-j 6`, best of 2, shared machine with load
  average 10; objects and dependency files are written to an empty directory,
  diagnostics are printed):

  | | wall | vs clang | CPU | vs clang |
  |---|---|---|---|---|
  | clang -O3 | 51.7 s | 1 | 292.8 s | 1 |
  | vpp, direct, one process per file | 13.2 s | 3.9× | 71.2 s | 4.1× |
  | vpp, VIR, one process per file | 14.6 s | 3.5× | 79.8 s | 3.7× |
  | `vpp --batch`, direct | 0.97 s | 53× | 5.2 s | 57× |
  | `vpp --batch`, VIR | 1.63 s | 32× | 9.1 s | 32× |

  The batch mode's 2022 objects and dependency files are byte-identical to
  those from one process per file (the harness checks it), and the objects are
  the same as in the make-built trees that passed FATE.  The wall time varies
  with the load (0.85-1.6 s direct, 1.55-3.4 s VIR in individual runs); the
  CPU time is stable.  The difference against one process per file is shared
  caches (31 000 of 38 000 includes are replayed from the header cache, the
  declaration cache skips 10.9 M tokens) and about 35 ms startup per process.
  Not measured: a build through make with vccd, which is the way a user gets
  the batch mode's caches; ar and linking are not included.
- **Run time** (`speed`, `-threads 1`, CPU, one round; not addressed, to be
  taken later): in total VIR 1.70× clang, the direct emitter 2.88×.  Worst
  through VIR: mpeg4/mpeg2 encoding 3.15×, convolution 3.4×, gblur 2.7×, aac
  encoding 2.3×, unsharp 2.3×, ffv1 1.8–1.9×; the decoders 1.1–1.3×.

### SQLite register allocation (2026-11-13)

A nonvolatile 64-bit load used exclusively as a pointer now yields a pointer
directly. A late block parameter crossing a call can keep the last available
callee-saved register. On the original x86 branch (7e166718), the mapped SQLite
aMem reload count fell from 92.28M to 14.54M and three alternating native
speedtest1 series improved by 1.27–2.00%. Those timings predate the tree
reorganization. The current x86 lowering supports the same VCC_VL_MAP,
VCC_VL_FILTER and VCC_VL_PROBE measurements through bench/sqlite/vir-edits.py.

The bounded predecessor-choice experiment had inconsistent runtime results.
Its patch and measurements in results/vir-entry-choice-20261113.* refer to
7e166718. The compiler continues to use its existing predecessor policy.
See [the optimizer policy inventory](RESEARCH-optimizer-limits.md) for the
GCC and LLVM comparisons used in the investigation.
