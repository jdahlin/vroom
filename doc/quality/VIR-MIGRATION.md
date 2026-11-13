# VIR as the only path: what moves, what goes

**Decision (Johan, 2026-11-13):** VIR is used for everything.  A full kernel
build with VIR built but not lowered (`-O2 -fno-vir-lower`) took 81.2 s
against 79.1 s without, the same 36 247 of 36 269 jobs passing; the run with
real lowering (`-O2`) was under way when this was written.  A loss of 2-3 %
of build time on VIR is accepted.  Optimizations that live outside VIR move into it, as long as CPU
time does not grow significantly.

This replaces three decisions of `PLAN-quality.md`: the arm64 pilot on the
shared walker (nothing is left to converge), "fusing walks comes later"
(the direct path's passes are replaced, not tidied), and the open question
of target dispatch (a target is a VIR lowering and its tables).

Evidence: `doc/review/quality-backend-records.md` (section 3: the pass
lists), `quality-vir-build.md`, `quality-vir-ir.md`, `quality-passes.md`,
`quality-codegen-shared.md`, `quality-walker-a64.md`.  "Expected" below is a
hypothesis to measure, not a finding.

## End state

```
AST -> shared walker (one instance: the VIR builder) -> VIR
    -> VIR passes (one table, each named, timed, switchable, verified)
    -> register allocation -> lowering per CPU (reads the CPU's op table)
    -> machine records -> layout, branch relaxation -> text or object
```

Gone at the end: arm64's AST walker (`arch/a64/codegen/` `expr stmt call arith
lval value cond fp bind label` and the recipe files, about 9.5 k lines),
x86-64's direct lowering through `LO()` (`arch/x64/codegen/lower.c wide.c fp.c`
and the two-address pass), most of arm64's record optimizer, the second
instance of the walker and `walk/prefix.h`, the TU rounds that only arm64's
`need_fold` asks for, and the "try VIR, fall back to the direct emitter"
control flow.  wasm's walker is already switched off.

## Gates before anything is deleted

| # | Gate | Today |
|---|---|---|
| G1 | Every function compiles through VIR: no fallback | 834 of 2 210 kernel functions in the sample were walked by VIR to failure and then again by the direct emitter (`quality-passes.md`) |
| G2 | The kernel builds through VIR (its `-O2`) within 3 % of the direct build (`-O1`), and boots | run under way |
| G3 | PostgreSQL, SQLite and the C suites pass with VIR only, on arm64 and x86-64 | each exists as a harness |
| G4 | `-O0` debugging is no worse: `-g` line tables and variable locations (`vcc-dwarf.py`) | today `-g` is tested after the build, so a function is walked twice (`Q-VIRB-11`) |
| G5 | Everything the direct path handles and VIR stubs: inline asm (`build.c:1338` stubs it), `setjmp`, wide integers, complex, vectors, variadics, computed goto | list to be made from the reasons VIR fails (G1) |
| G6 | Compile time on code where emission dominates: SQLite's emit through VIR was 6.6 times the direct emitter's (3 335 M against 508 M instructions) | to be brought inside the accepted loss or explicitly accepted |

G1 and G5 are the same list seen from two sides: the first job is a counter
of VIR failures by reason over the kernel, PostgreSQL and SQLite.

## Passes on the direct path, and their fate

### arm64 record clean-ups (`arch/a64/codegen/opt.c`, `func.c:474`)

| Pass | What it does | On VIR | How to decide |
|---|---|---|---|
| unreachable, fallthrough (`codegen/rec/`) | drop dead records and jumps to the next label | stay, after lowering: they clean up what layout leaves | already shared kernels |
| liveness on records | blocks, successors, live sets | goes: VIR has the CFG and the allocator's liveness | no reader left once the passes below go |
| spill pairs | store and reload of a dead temporary | expected gone: a product of the direct emitter's temporaries | count what it removes on VIR output |
| copies (twice) | copy propagation, dead moves | expected mostly gone (SSA, coalescing); what is left is a missing allocator or edge-move improvement | count per function; fix in the allocator |
| slots | load to move, dead stores on frame slots | moves: store-to-load forwarding and dead-store elimination on VIR's slots (promotion already does the scalar case) | count; write the VIR pass for what remains |
| exts | redundant extension removal | moves: VIR's known-bits and `low32` facts already exist (`vir/facts.c`) | count |
| dead | pure dead results | expected gone (VIR's dead-code pass) | count |
| fold (VIR only) | shifts, extensions, sums, constants folded into the reader; byte loads | stays in lowering as instruction selection, written as patterns in the op table, not as a sweep over records | needed by construction |
| flags (VIR only) | repeated compares | moves: compare reuse is a VIR value-numbering fact | count |
| `opt_regs`, prologue marker, `opt_noframe`, tail expansion, frame-slot rewrite, `tbz` relaxation | registers used, frame decisions, branch range | stay as the fixed tail of lowering, one sweep each or fused | they are 6 walks at `-O0` today |

The review measured the clean-ups after VIR's allocator at 389 M
instructions, 10.5 % of a VIR compile of SQLite.  "Count" means: with a
per-pass counter, how many records each pass changes on VIR output over the
kernel sample and SQLite.  A pass that changes nothing is deleted; one that
still changes something names the VIR pass or allocator fix that replaces it.

### x86-64 record passes (`arch/x64/codegen/`)

`x64_dead` (gated, capped at 256 blocks), `x64_twoaddr` (in `print.c`) and
the copy scans exist only for the direct path through `LO()`.  x86-64's VIR
lowering (`arch/x64/codegen/vir.c`) already produces its own two-address form.
They go with the direct path.

### Decisions the walker takes on the AST (`codegen/opt/`, `codegen/walk/`)

| Recipe | Files today | On VIR |
|---|---|---|
| division by a constant, by a power of two; multiplication by a constant, high multiply | `codegen/opt/{sdivconst,udivconst,sdivpow2,divpow2,mulconst,mulshift}.c` plus an emitting copy per walker (15 files for 4 recipes) | one VIR rewrite each (the plan structs `em_*_recipe` stay), costed per CPU through the op table |
| switch: tree, table, bitset, value table | `codegen/opt/{switch,switch-values}.c`, three emitting copies | a VIR switch-lowering pass; the dense-switch rule is written twice in the lowerings and already differs (`Q-VIRB-02`) |
| identities, double unary, xor-not, compare with zero, compare bounds, repeated operations | `codegen/opt/{identity,double-unary,xor-not,cmpzero,cmpbounds,repeated}.c` | VIR simplification rules (`vw_simplify` has part of them: find the overlap first) |
| constant truth of a condition, unused initializers, known values of locals and array elements | `codegen/opt/{control,unusedinit,locals,sibling-values}.c`, `wk_condfold`, the two known-value tables (`Q-CGS-03`, `Q-CGS-08`) | constant propagation and dead-store elimination on VIR; the walker keeps only what a diagnostic needs |
| tail calls | `codegen/opt/tail.c`, `arch/*/emit/tail.c` | a VIR property of the call, expanded in lowering |

The walker then only translates: it should not fold, select or track values.
That is also what makes it reusable for C++.

### Rewrites in the front end done for optimization

`fold_local_builtins`, `object_size_locals` (`frontend/c/func.c`), and the lowering
of complex numbers, wide atomics and `_BitInt` casts into AST
(`quality-cc-expr.md` Q-ccexpr-04) are candidates, case by case: what a
diagnostic or a constant expression needs stays in the front end; the rest
becomes VIR.

## VIR's own structure first

Moving passes into `codegen/vir/build.c` as it is would make the worst file
worse (18 jobs, about 20 sweeps, the vectorizer by `#include`).  Before any
pass moves:

1. **A pass table**: name, function, what it needs and invalidates; a switch
   to time, disable and verify each pass by name (`VCC_VIR_PASSES`, now
   `-fvir-passes`, was in SPEC-vir 22.6 and had no code).
2. **One pass per file**, the pre-IR in its own header (`Q-VIRB-01`).
3. **Analysis validity recorded**, so dominators (3 runs per function today)
   and the CFG (5) are computed once (`Q-vir-ir-01`, `Q-passes-03`).
4. **Where the optimizer lives**: decided, on VIR itself, not on the
   builder's pre-IR (`Q-VIRB` decision 1; `DESIGN-vir-optimizer.md`,
   WORK-vir.md).  Every moved pass is written on VIR.
5. **arm64's lowering out of the generic directory** (`codegen/vir/a64.c` to
   `arch/a64/`), and one lowering driver with per-CPU callbacks: the two
   lowerings share only two helper files today (`Q-VIRB-02`, `Q-VIRB-03`).

## Order

1. The failure counter (G1) and the per-pass counters on the record
   clean-ups.  Small, no behaviour change, gives every later step its numbers.
2. VIR's structure, items 1-5 above, identical output.
3. Close G1/G5 reason by reason, most frequent first.
4. Move passes in the order the counters give: first those that only need
   deleting, then slots, exts and flags, then the walker's recipes
   (division by a constant as the trial).
5. When G1-G6 hold on arm64: delete arm64's walker and record optimizer.
   Then x86-64's direct lowering.

## Validation

Moving a pass changes generated code, so identical output cannot prove it.
For each move: the C suites under qemu and natively, kernel build and boot,
PostgreSQL's `meson test`, SQLite's testrunner; instructions per TU on the
kernel sample and SQLite before and after (budget: the accepted 2-3 % in
total, tracked as a running sum in this file); code size of the kernel image
and run time of `speedtest1` and `zbench` no worse than the direct path,
which stays available as the reference until step 5.

## What this does to the quality plan

- Module order: VIR and its builder move up, right after the shared library
  and before the code generators.
- `arch/a64/codegen`'s walker and `arch/x64/codegen`'s direct lowering get **no**
  quality pass (no renames, no splits, no comments): they are scheduled for
  deletion.  Only what survives is cleaned: ABI, inline asm, the op table,
  printer, encoder, the tail of `finish_function`.
- The 800 lines of target-independent code that sit in `arch/a64/codegen`
  still move out (they are used by the walker and by VIR).
- One record type (`ins` = `rec`) and one op table per CPU stay as decided;
  they are what lowering targets.
