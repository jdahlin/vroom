# Optimization levels

vcc has two code generators.  The **direct emitter** walks the AST and emits
machine records, cleaning them up afterwards.  **VIR** (`PLAN-vir.md`,
`SPEC-vir.md`) builds an SSA function from the same walker, optimizes it,
allocates registers and lowers it to the same records.  The `-O` level picks
the code generator and what runs in it; nothing else does.  There are no
environment variables for optimization.

The levels are resolved in the driver (`src/driver/optimization.c`) into
`cc_opts.opt` (0-3) and `cc_opts.optimize`.  The options a level turns on are
one table, `CC_OPTIMIZATIONS` in `src/include/frontend/c/cc.h`.  Under vccd the
options are per job, like every other option.

## The levels

| Option | Level | Code generator | `__OPTIMIZE__` |
|---|---|---|---|
| `-O0` (default) | 0 | direct emitter, unoptimized | no |
| `-O1`, `-Og`, `-O` | 1 | direct emitter | yes |
| `-O2`, `-Os`, `-Oz` | 2 | VIR; the direct emitter of `-O1` for what VIR cannot build | yes |
| `-O3`, `-O4` and up, `-Ofast` | 3 | as `-O2`, with the loop vectorizer | yes |

`-Os` and `-Oz` also define `__OPTIMIZE_SIZE__`; vcc makes no size-specific
choices yet, so their code is `-O2`'s.  `-Ofast` sets the level only: as with
clang, `-ffast-math` is its own option.  The last `-O` on the command line wins.

### -O0

Every local lives in its frame slot; an expression's value goes through
scratch registers.  Only what C requires is folded (constant expressions),
and only `always_inline` functions are inlined.

### -O1: the direct emitter

What the direct emitter does when `cc_opts.opt >= 1`:

- **Front end**: constant folding across a function body
  (`frontend/c/function-body-fold.c`), library calls known by their arguments
  (`builtin-library-call.c`).
- **Walker** (`codegen/walk/`, `codegen/opt/`): multiplication and division by
  constants as shifts and multiplies (`mulconst.c`, `udivconst.c`,
  `sdivconst.c`, `divpow2.c`), comparisons against zero and known bounds
  (`cmpzero.c`, `cmpbounds.c`), repeated subexpressions (`repeated.c`),
  `~x ^ y` and double unary operators, unused initializers (`unusedinit.c`),
  values a sibling call can pass on (`sibling-values.c`); `switch` as a jump
  table, a bit set or a balanced compare tree (`switch.c`, `switch-values.c`).
- **Inlining**: static and `inline` functions of up to 40 AST nodes.
- **Registers**: the six most used locals of a function (by loop depth, from
  the prepass) live in callee-saved registers.
- **Calls**: sibling calls as tail calls (`-fno-optimize-sibling-calls` turns
  them off).
- **Records** (`arch/a64/codegen/opt.c`, `func.c`): the clean-up passes over
  each function's machine records: liveness, copies, slots, extensions, dead
  records, folds, flags, unreachable code and fall-throughs, a frame only when
  needed, branch relaxation (`bench/record-passes.py` counts what each
  changes).
- **x86-64**: the frame pointer is omitted in leaves (`-fno-omit-frame-pointer`
  keeps it).

### -O2: VIR

Every function goes through VIR first (`codegen/vir/try.c`).  A function VIR
cannot build or lower is compiled by `-O1`'s direct emitter, so `-O2` code is
never worse than "correct".  The reasons are in `codegen/vir/failure.h`;
`bench/vir-failures.py` counts them over the suites, a kernel sample,
PostgreSQL and SQLite.  The main ones today: inline assembly, records passed
or returned by value, variadic functions and `__int128`.  `-g` changes no
code: both lowerings write a line-table row at each block and wherever the
source location of VIR's instructions changes (`vir_loc`, SPEC-vir.md 22.7;
`bench/vcc-dwarf.py -O 2`, `bench/vcc-opt-debug.py`).

What a function gets, in order (the rows of `codegen/vir/pass-table.c`):

1. **The walk**, with inlining per call site: a static function called once
   is inlined up to 4000 AST nodes, other callees up to 150 nodes counting
   what they inline themselves (600 when constant arguments fold them).
2. **CFG clean-up** (`thread`, `rpo`, `preds`, `seal`; `cfg.c`): jumps to
   jumps threaded, unreachable blocks dropped, SSA construction completed.
3. **Promotion** (`promote`; `promote.c`): frame slots whose address does
   not escape become SSA values.
4. **Block parameters** (`trivial`, `dead-params`; `parameters.c`): trivial
   parameters forwarded, unused ones dropped.
5. **Simplification** (`simplify`; `rules.c`, `value-numbering.c`,
   `load-forwarding.c`, `simplify.c`): algebraic rules, value numbering over
   the dominator tree, loads forwarded from stores and earlier loads, dead
   stores to slots and dead code removed.  Steps 3-5 run a second time when
   simplification freed a slot's address.
6. **Condition merge** (`condition-merge.c`): `a && b` and `a || b` whose
   result is a value become one compare chain (arm64: `ccmp`).
7. **If-conversion** (`if-conversion.c`): diamonds and triangles with small
   arms become selects (`csel`, `cmov`).
8. **Edge splitting**, conversion to VIR and its canonical form (`split`,
   `slot-uses`, `to-vir`, `canon`).
9. **Register allocation** (`vir/alloc.c`, `SPEC-vir.md` 15): greedy, in block
   order, on SSA, with parallel moves on edges; callee-saved registers chosen
   per function.
10. **Lowering** (`arch/a64/vir/`, `arch/x64/vir/`): block layout with loop
    rotation (the test at the bottom of a loop) and duplication of a loop's
    entry test; on arm64, shrink-wrapping (the prologue only on the paths
    that need a frame; not with `-fasynchronous-unwind-tables` on ELF).
11. **Records**: the same clean-up passes as `-O1`.

### -O3: the vectorizer

`-O2`, plus:

- **The loop vectorizer** (`codegen/vir/vectorize.c`, `RESEARCH-vectors.md`):
  innermost loops of straight-line bodies with 4- and 8-byte elements at
  constant strides become 128-bit vector loops in front of the scalar loop,
  which runs the rest (NEON on arm64, SSE2 on x86-64); inductions, integer
  sums and ordered floating-point reductions (no reassociation).  A loop whose
  accesses it cannot prove separate stays scalar; so does a loop that is only
  a search (a maximum and its index).
- **Loop constants** (`codegen/vir/loop-constants.c`): in an innermost loop
  without calls, up to six constants that cost instructions each round (a
  symbol's address, a floating-point constant, a compare's bound that is not
  an immediate) are made once in front of the loop.

Neither runs under `-mgeneral-regs-only` (the kernel's flag).

## Options for one optimization

Each row of `CC_OPTIMIZATIONS` is `-fNAME` and `-fno-NAME`, on from its level
up.  An option overrides the level whatever their order.  None of them has an
effect below `-O2`: VIR is `-O2`'s code generator, and `-fvir` does not bring
it to a lower level.

| Option | From | What it does |
|---|---|---|
| `-fvir` | `-O2` | functions go through VIR; `-fno-vir` gives `-O1`'s code at `-O2` |
| `-frotate-loops` | `-O2` | the lowering puts a loop's test at its bottom |
| `-fshrink-wrap` | `-O2` | arm64: the prologue moves to the paths that need a frame |
| `-fvectorize` (`-ftree-vectorize`) | `-O3` | the loop vectorizer |
| `-fhoist-loop-constants` | `-O3` | an innermost loop's constants made once in front of it |

The direct emitter's own optimizations have no options of their own, apart
from the ones clang has: `-fno-jump-tables`, `-fno-optimize-sibling-calls`,
`-fomit-frame-pointer`.

## Developer options for VIR

`-fvir-*` options are aids for testing and measuring VIR.  They are accepted
by vpp, vcc and vccd jobs alike, but are not in vccd's capability table, so
kbuild's probes do not see them.  They have no effect below `-O2`.

| Option | What it does |
|---|---|
| `-fno-vir-lower` | build VIR (and verify, dump, report it), then compile the function with the direct emitter |
| `-fvir-verify` | the verifier runs on every function built; a rejected one falls back, with a message |
| `-fvir-dump` | each function's VIR on stderr |
| `-fvir-report` | a line per function on stderr: `vir: F: lowered, N spills`, or why not |
| `-fvir-check` | the allocation checker runs on every function; a failure falls back, with a message |
| `-fvir-stats` | per TU on stderr: functions through VIR and why the others were not (`bench/vir-failures.py`) |
| `-fvir-passes=ITEMS` | leave out (`-NAME`), select (`only=A+B`), add (`+NAME`), check (`verify`, `verify=A+B`, `analyses`) or time (`time`, printed with `-fvir-stats`) the builder's passes; `codegen/vir/pass-table.c` |
| `-fvir-functions=FILE` | only the functions named in FILE, one per line, are lowered (bisecting a miscompile) |
| `-fvir-vectorize-report` | a line per innermost loop the vectorizer looked at: vectorized, or why not |
| `-fvir-ccmp=select\|all\|none` | which compare pairs the condition merge joins: where if-conversion follows (default), every pair, none |
| `-fvir-probe=MASK` | a `nop` after each allocator edit of the kinds in MASK (bits 0-7 on both targets; x86-64 adds 8 definition store, 9 lowering, 10 edge exchange), to count them by instructions retired (`arch/a64/vir/moves.c`, `arch/x64/vir/moves.c`) |
| `-fvir-map` | x86-64: each allocator edit between `# vl-site` and `# vl-end` comments in the assembly, and the allocator's decisions (`vl-function`, `vl-entry`, `vl-slot`) on stderr (`bench/sqlite/vir-edits.py`) |
| `-fvir-map-function=NAME` | x86-64: `-fvir-map` and `-fvir-probe` in function NAME alone; never changes another function's code |

The harnesses take the level as an option: `bench/vcc-ctests.py -O1|-O2|-O3`
(vcc's level; clang's reference stays at `-O2`) and `--vflags` for vcc-only
flags, `bench/vir-failures.py --level`, `bench/sched-measure.py emit -O`.

## Measurements (2026-11-13, M1 Pro, shared machine under load)

| | `-O0` | `-O1` | `-O2` | `-O3` |
|---|---|---|---|---|
| C suites, `vcc-ctests.py` (2129 tests) | | 2129 pass | 2129 pass | 2129 pass |
| TSVC_2 checksums (151) | | 151 | 151 | 151 |
| TSVC_2, G cycles (clang -O3: 4.77) | | 23.76 | 9.30 | 6.35 |
| sqlite3.c `-c`, wall, 1 thread | 0.17 s | 0.21 s | 0.68 s | 0.70 s |
| sqlite3.c `__text` bytes | 1 216 396 | 933 500 | 1 252 108 | 1 253 268 |

`-O2` code is larger than `-O1`'s because of the inliner.  Compile cost of
the switch, `bench/vbench.py gate` against the commit before it (the kernel
corpus, `-c -O2`, one thread, instructions retired): the front end and the
assembler unchanged, code generation +90 % (708 to 1348 M instructions),
the whole compile +9.5 % (+18 % cycles).  PLAN-vir.md's V7 proposes at most
+15 % for `-O2` compilation in total.  The arm64 kernel
(`bench/vccd-kbuild.sh`, `-O2`, now through VIR) builds in 17.1 s at `-j 6`
on the loaded machine and boots Debian (`bench/boot-test.py`); VIR lowers
about half of its functions (inline assembly is most of the rest).
