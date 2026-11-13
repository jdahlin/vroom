# Work order: VIR and its builder, steps 1-3 of the migration

The module pass for VIR, its builder and its two lowerings, written to be
carried out by a cheaper model and scripts.  It covers steps 1 to 3 of the
"Order" in `doc/quality/VIR-MIGRATION.md`, which is binding: **VIR becomes the
only code generation path**.  Steps 4 and 5 (moving passes, deleting the
direct emitters) are later orders.  Evidence: `doc/review/quality-vir-build.md`
(`Q-VIRB-NN`), `quality-vir-ir.md` (`Q-vir-ir-NN`), `quality-passes.md`
(`Q-passes-NN`), `doc/PLAN-vir.md`, `doc/SPEC-vir.md`.  Names follow
`doc/STYLE.md`.

The reviews' line numbers are those of main at 2e6d3e73, before the tree
moved, and PR #141 shifts `build.c` and `a64.c` again: find code **by the
function name** given here, never by line.  Line numbers below are hints
from main at d602dfa2.

**Starts when** step 1 is on main: PR #140 is merged; PR #141 (open) merges.
Step 2 needs nothing else.  `WORK-common.md` step 2 rewrites every file of
`src/` by script; whichever of the two lands second merges `origin/main`
(a merge commit) and reruns `src/tools/rename-table.py` with
`doc/quality/rename-common.tsv` on the files it added.

**Owns** (no other order edits these; this order edits nothing else):

- `src/vir/` (all), `src/include/vir/` (all), `src/bin/virtest/`,
  `src/bin/viralloc/`, `src/test/vir/`, `src/test/vir-alloc/`;
- `src/codegen/vir/` (all, and the files step 2 creates there);
- `src/arch/x64/codegen/vir.c` until step 2d moves it, then `src/arch/x64/vir/`;
- `src/arch/a64/vir/` (new in step 2d; the arm64 lowering);
- `src/codegen/walk/`, **only** for the additions step 3 needs, under the
  rule of step 3 ("Walker edits"); its split, renames and comments are not
  in this order;
- in `src/Makefile`: the lines `vir_SRC`, `VIRA_SRC`, `VIRALLOC_SRC` and
  `codegen_SRC`;
- `bench/vir-*.py`, `bench/record-passes.py`, the new `bench/vir-passes.py`;
- `doc/PLAN-vir.md`, `doc/SPEC-vir.md`, `doc/quality/VIR-MIGRATION.md`, and in
  `AGENTS.md` only the bullets that name the files above.

`src/codegen/emitimpl.h` and `src/codegen/tu.c` are owned by
`WORK-codegen-surviving.md`.  This order does not edit them: state this
order needs lives in the builder's state (`vwst`, reached as `e->vw`), and
prototypes go in `src/codegen/vir/preir.h`.  If a step cannot do without a
line in either file, it stops and says so in its report.

Calls into code owned by WORK-codegen-surviving (`finish_function`,
`x64_finish`, the record constructors of `a64impl.h` and `x64impl.h`,
`note_ref`, `logimm_ok`, `sym_needs_got`, `abi_align`) are made, never
edited.  A step that needs a new arm64 or x86-64 record opcode asks that
order for a one-row PR to its op table and waits.

## What done looks like

- Step 1: the two counters are on main and their tables are the baseline
  below.
- Step 2, with identical output: `codegen/vir/build.c` and `vec.inc` are one
  pass per file with the pre-IR in its own header; the builder's passes run
  from one table with a switch to time, disable and verify each by name;
  `src/vir/` records which analyses are valid, so dominators and the CFG are
  computed once per function; both lowerings live under `src/arch/<cpu>/vir/`,
  split by job, and share one block driver.
- Step 3: `bench/vir-failures.py` reports zero fallbacks on the suites,
  the kernel sample, PostgreSQL and SQLite (gate G1), including with `-g`
  (G4), with the instruction cost inside the budget of VIR-MIGRATION.md.

## Steps

Each step is one PR (step 3: one PR per sub-step).  Proof for every
identical-output step (all of step 2), from the worker rules and AGENTS.md:

```
nice -n 10 make -C src -j 3 vcc OUT=$PWD/build/u                     # unity
nice -n 10 make -C src -j 3 vcc VCC_UNITY= OUT=$PWD/build/p          # file by file
bench/vbench.py mkbase REV                                           # base from the branch point
bench/vcc-same.py --base DIR -j 3                   (VCC_BIN=$PWD/build/u)
VCC_VIR=bl bench/vcc-same.py --base DIR -j 3        # the VIR path, identical too
VCC_VIR=bl bench/vcc-ctests.py -j 3 --suite vcc --no-cache --bin-dir build/u
bench/vir-test.py -j 3 --bin-dir build/u ; bench/vir-alloc.py ; bench/vir-exposure.py
python3 bench/vcc-vir-x86.py --bin-dir build/u      # x86-64's VIR lowering (CI's x86-64 job runs it too)
```

`vcc-same.py`'s known 8 differences (mode `e` on files that include
`src/headers/`, the base tree's path in line markers) are not differences.
Timing (`bench/vbench.py gate`, with and without `VCC_VIR=bl`) is listed
under "Proof not run: needs a quiet machine" in the PR body; the integrator
runs it.

### 1. The counters (done or landing)

| PR | What | State |
|---|---|---|
| #140 | `VCC_REC_STATS=1`, `bench/record-passes.py`: records each arm64 clean-up pass changes, direct path against VIR | merged |
| #141 | `src/codegen/vir/failure.h` (one X-macro table of reasons), `VCC_VIR_STATS=1`, `bench/vir-failures.py` | open; this step is its merge |

Starting numbers, from the PR bodies (arm64, `-O2`).

**Failures by reason** (#141, `bench/vir-failures.py --top 12`, `VCC_VIR=bl`):

| Set | Functions | Through VIR | Fell back |
|---|---|---|---|
| suites (2280 TUs; 58 without a statistic, 6 more exit nonzero) | 19 437 | 17 932 (92.3 %) | 1 505 |
| kernel (150 TUs of `work/build-o0`, evenly spaced) | 4 229 | 2 303 (54.5 %) | 1 926 |
| PostgreSQL (150 TUs, `-g` removed) | 2 428 | 2 150 (88.6 %) | 278 |
| SQLite amalgamation | 1 470 | 1 464 (99.6 %) | 6 |
| all | 27 564 | 23 849 (86.5 %) | 3 715 |

| Reason | suites | kernel | PostgreSQL | SQLite | all | Example |
|---|---|---|---|---|---|---|
| `inline_assembly` | 178 | 1 686 | 3 | | 1 867 | `arch/arm64/kernel/cpufeature.o:this_cpu_has_cap` |
| `record_argument_or_result` | 785 | 126 | 210 | 6 | 1 127 | `vcc/bfloat16:narrow`, `sqlite3.c:sqlite3RunParser` |
| `variadic_function` | 186 | 26 | | | 212 | `vcc/c23_va_start:total` |
| `wide_integer` | 117 | 80 | 12 | | 209 | `arch/arm64/lib/csum.o:csum_ipv6_magic` |
| `unsupported_instruction` (`acas`, `axchg`) | 16 | | 49 | | 65 | `xlogrecovery.c:HotStandbyActive` |
| `unsupported_floating_type` | 48 | | | | 48 | `vcc/atomic_complex_incdec:main` |
| `stack_pointer` | 37 | | | | 37 | `vcc/align_local:f` |
| `switch_bit_set` | 18 | 6 | 3 | | 27 | `vcc/switch_bitsets:bs32` |
| `frame_address` | 22 | 1 | | | 23 | `vcc/frame_depth:walk` |
| `alloca` | 15 | | | | 15 | `vcc/align_local:main` |
| `access_outside_slots` | 14 | | | | 14 | `vcc/ms_struct_layout:main` |
| `computed_goto` | 9 | | | | 9 | `vcc/label_declaration_lifetime:indirect` |
| `over_aligned_slot`, `unsupported_builtin` | | 1 | 1 | | 2 | |
| 11 more in the suites, each at most 8 | | | | | | |
| `debug_info` (PostgreSQL with its own `-g`, `--keep-g`) | | | 2 697 of 2 697 | | | every function |

**Record clean-ups after VIR** (#140, `bench/record-passes.py -j 3 -n 200`,
records changed + removed + added, functions VIR lowered; the direct path's
kernel column for contrast):

| Pass | suites | kernel | SQLite | kernel, direct path |
|---|---|---|---|---|
| `fallthrough` | 150 509 | 33 280 | 50 916 | 65 822 |
| `frame_rewrite` | 25 204 | 9 084 | 31 439 | 119 920 |
| `tail_expand` | 21 376 | 9 605 | 7 030 | 0 |
| `fold` (VIR only) | 14 394 | 4 195 | 8 910 | 0 |
| `exts` | 5 479 | 3 102 | 1 576 | 42 834 |
| `copies_1` | 3 430 | 987 | 2 457 | 329 489 |
| `flags` (VIR only) | 1 236 | 138 | 245 | 0 |
| `expand_tables` | 601 | 349 | 1 092 | 2 361 |
| `slots` | 433 | 467 | 410 | 80 700 |
| `copies_2` | 346 | 23 | 58 | 60 719 |
| `dead` | 197 | 322 | 2 639 | 9 081 |
| `opt_noframe`, `tbz_relax` | 20 | 33 | 49 | 0 |
| `spill_pairs`, `liveness`, `opt_regs`, `unreachable` | 0 | 0 | 0 | 31 896 (`spill_pairs`) |

Functions through the clean-ups: suites 17 908 VIR / 19 444 direct, kernel
4 026 / 7 342, SQLite 1 464 / 1 916.  These feed step 4 (not this order).

Proof of step 1: #141's own (in its body); after the merge,
`bench/vir-failures.py -j 3` and `bench/record-passes.py -j 3 -n 200` print
tables matching the above within the sample's noise.

### 2. Structure, identical output

Five sub-steps, one PR each.  Order and parallelism:

| Sub-step | Edits | Starts after | Runs in parallel with |
|---|---|---|---|
| 2a builder split | `src/codegen/vir/*`, the `#include "vw.h"` line in `codegen/vir/a64.c` and `arch/x64/codegen/vir.c` | step 1 | 2c |
| 2b pass table | `codegen/vir/pipeline.c`, `try.c`, new `pass-table.[ch]`, `preir-check.c`, `bench/vir-passes.py` | 2a | 2c, 2d |
| 2c analysis validity | `src/vir/`, `src/include/vir/` only | step 1 | everything |
| 2d lowerings to `arch/<cpu>/vir/` | `codegen/vir/a64.c`, `arch/x64/codegen/vir.c`, the new directories, `src/Makefile` `codegen_SRC` | 2a | 2b, 2c |
| 2e one lowering driver | `arch/a64/vir/*`, `arch/x64/vir/*`, new `codegen/vir/lower-driver.c` | 2d | 2b, 2c |

2c is in progress on branch `quality/vir-analysis-validity` (a worktree
exists, no commits on it yet as of d602dfa2).  If that branch's PR merges
with the content below, 2c is done; otherwise this section is its brief.

#### 2a. One pass per file in the builder (moves only)

First commit: `git mv src/codegen/vir/vw.h src/codegen/vir/preir.h` and
update its three includers.  Second commit: the moves below.  A function
that becomes visible across files changes `static` to `VPRIV` and gets its
prototype in `preir.h` (or `simplify.h`); nothing else changes.  All files
stay in the `codegen` unity TU (`codegen_SRC` is a wildcard over
`codegen/vir/*.c`; `.inc` files are not compiled alone).  Every new file
starts with `#define WK_PREFIX vw_` / `#define LO_PREFIX vw_` and the three
includes `build.c` has today (walk.h, proto.h, preir.h), so that it compiles
alone with `VCC_UNITY=`.

| New file | Functions and definitions (from `build.c` unless named) | Section of Q-VIRB inventory | Finding |
|---|---|---|---|
| `preir.h` | all types of `vw.h`; `VW_NONAME`, `VW_FNAME0`, `VW_RETV`, `VW_VAR0`, `VW_NOBLK`, `GROW`, `VW_EACHOP`; inline `vw_find`, `vw_isval`, `vw_ty`; prototypes | (a), parts of (h) | Q-VIRB-01 |
| `preir.c` | `vw_st`, `vw_fail`, `vw_put`, `vw_ins_in`, `vw_ins`, `vw_k`, `vw_iconst`, `vw_xput`, `vw_newblk`, `vw_lblk`, `vw_term`, `vw_br` | (a) | Q-VIRB-01 |
| `ssa.c` | `vw_hkey`, `VW_H0`, `vw_hgen`, `vw_hslot`, `vw_write`, `vw_phity`, `vw_newphi`, `vw_phiops`, `vw_undef`, `vw_readv` | (b) Braun | Q-VIRB-01 |
| `lower-value.c` | conversions `vw_conv_in` .. `vw_vty`; registers `vw_treg` .. `vw_resname`; integer `vw_umulhi` .. `vw_switch_table` and the stubs `vw_ldmem` .. `vw_wcvt`; floating point `vw_fty` .. `vw_fbits` | (c) | Q-VIRB-01 |
| `lower-memory.c` | slots and memory `vw_slot` .. `vw_zero`; locals `vw_locals_begin`, `vw_lhash`, `vw_local`, `vw_undecl` | (c) | Q-VIRB-01 |
| `lower-control.c` | builtins and control `vw_bitop` .. `vw_named` (atomics, the stubs `vw_tlsaddr` .. `vw_asm`, `vw_va_start`, `vw_va_arg`, `vw_retrec`, `vw_tail_return`, `vw_trap`, `vw_barrier`, `vw_dbgrow`, `vw_label`, `vw_jump`, `vw_jumpr`, `vw_named`) | (c) | Q-VIRB-01 |
| `lower-call.c` | `vw_classify`, `vw_group`, `vw_pcty`, `vw_word`, `vw_sig`, `vw_call`, `vw_params`, `vw_ret`; `vw_begin`, `vw_finish`, `vw_ecx_free` | (c), (d) | Q-VIRB-01 |
| `cfg.c` | `vw_nsucc`, `vw_succp`, `vw_thread`, `vw_rpo`, `vw_preds`; `vw_doms`; `vw_split` | (e), (k), (p) | Q-VIRB-01 |
| `parameters.c` | `vw_trivial`, `vw_mark`, `vw_deadparams` | (f) | Q-VIRB-01 |
| `promote.c` | `VW_NOCONV`, `vw_ldconv`, `vw_rel`, `vw_escape`, `vw_entry`, `vw_zerok`, `vw_zeroset`, `vw_mscov`, `vw_promdead`, `vw_promote` | (g) | Q-VIRB-01 |
| `simplify.h` | `vwhe`, `vwsimp`, `vwaddr`, the "simplification" block comment, prototypes shared by the four files below | (h)-(m) | Q-VIRB-01 |
| `rules.c` | `vw_inc`, `vw_dec`, `vw_kint`, `vw_zw`, `vw_sw`, `vw_undefk`, `vw_simp1`, `vw_commutes` | (h) | Q-VIRB-01 |
| `value-numbering.c` | `vw_hlook`, `vw_kcanon`, `vw_cse_blk` | (j) | Q-VIRB-01 |
| `load-forwarding.c` | the "load across blocks" comment, `VW_FAROFF`, `VW_FARMAX`, `vw_baseoff`, `vw_unwritten`, `vw_far_init`, `vw_far` | (i) | Q-VIRB-01 |
| `simplify.c` | `vw_ownaddr`, `vw_simplify` (the driver and dead-code removal) | (l), (m) | Q-VIRB-01 |
| `if-conversion.c` | `vw_exprsame`, `vw_armreuse`, `vw_armempty`, `vw_hoist`, `vw_predix`, `vw_ifconv` | (n) | Q-VIRB-01 |
| `condition-merge.c` | `vw_cbzlike`, `vw_cchain`, `vw_condmerge` | (o) | Q-VIRB-01 |
| `vectorize.c` | all of `vec.inc` except `vv_pin` (analysis, construction, `vw_vectorize`) | vec.inc | Q-VIRB-01 |
| `loop-constants.c` | `vv_pin` and what only it uses | vec.inc 1401-1476 | Q-VIRB-15 (the move only; its gate stays `VCC_VIR_VEC`) |
| `to-vir.c` | `vw_m`, `vw_fin_blk`, `vw_emit_ins` | (q) | Q-VIRB-01 |
| `pipeline.c` | `vw_emit` | (r) | Q-VIRB-01 |

`build.c` and `vec.inc` are deleted at the end (`git rm`); `build.c`'s
opening comment moves to the top of `preir.h`.  If `vw_predix` turns out to
be used by `vw_condmerge` too, it goes to `cfg.c` instead.  The trailing
`#include "vec.inc"` disappears; `vectorize.c` includes `preir.h`.

Proof adds: `bench/vcc-tsvc.py` (151 checksums and the vectorized-loop
count unchanged); `src/tools/gen-walkprefix.py --check`.

#### 2b. The pass table and `VCC_VIR_PASSES`

Two commits: (1) the moves out of `vw_emit` into named functions
(`vw_seal` from the sealing block, `vw_slot_uses` from the dead-slot scan,
`vw_to_vir` from the emission loop: moves only); (2) the table and the
switch.

The table (`src/codegen/vir/pass-table.c`, type in `pass-table.h`):

```c
typedef struct vir_pass {
	const char *name;          /* stable; VCC_VIR_PASSES and the statistics use it */
	bool (*run)(ecx *e);       /* returns whether it changed anything */
	uint8_t needs;             /* PIR_ORDER, PIR_PREDS, PIR_DOMS: facts it reads */
	uint8_t keeps;             /* the facts still valid after it; the others are cleared */
	uint8_t flags;             /* PASS_REQUIRED, PASS_THEN_TRIVIAL, PASS_IF_PREVIOUS_CHANGED, PASS_VECTOR_GATE */
} vir_pass;
```

One row per step of `vw_emit` today, in its order (Q-VIRB 3.1):
`thread`, `rpo`, `preds`, `seal`, `promote`, `trivial`, `dead-params`,
`simplify`, `promote-again`, `trivial-again` / `dead-params-again` /
`simplify-again` (each `PASS_IF_PREVIOUS_CHANGED` on `promote-again`),
`condition-merge` and `if-conversion` (each `PASS_THEN_TRIVIAL`: the
`while (vw_trivial(e))` after them), `vectorize` and `loop-constants` (both
`PASS_VECTOR_GATE`: run only where `vw_vectorize` runs today, so
`VCC_VIR_VEC=0` still turns both off), `split`, `slot-uses`, `to-vir`,
`canon` (`vir_canon`).  `trivial`'s fixed point is the loop in the driver,
not inside the pass.  A row is `PASS_REQUIRED` unless `VCC_VIR=blv` (the
verifier) and `VCC_VIR=bl bench/vcc-ctests.py -j 3 --suite vcc` both pass
with it disabled; the PR lists which rows qualified.  `pipeline.c`'s
`vw_emit` becomes a loop over the table that does exactly what it does
today when the switch is unset.

The switch, read once per process (`pthread_once` in `pass-table.c`, not
per function: Q-VIRB-10):

```
VCC_VIR_PASSES=ITEM[,ITEM...]
  -NAME          skip pass NAME (a required one: error)
  only=A+B+...   run exactly these, in table order, plus the required ones (SPEC-vir 16: "overrides the pass list")
  verify         after every pass: the pre-IR checker (before to-vir) or vir_verify (to-vir, canon)
  verify=A+B     ... after the passes named
  time           per pass: runs, runs that changed something, instructions (vperf, read only when set)
```

An unknown name prints `vcc: VCC_VIR_PASSES: unknown pass NAME (passes:
...)` to stderr and exits 2 (a test aid, not an option).  `time` and
`verify` output goes to stderr, one line per pass per TU, printed by
`vw_stats_print` (`try.c`, from #141) when `VCC_VIR_STATS=1` is also set:

```
vcc vir-pass: pass=NAME runs=N changed=N instructions=N
```

Per-TU counters live in a small mutex-protected table in `pass-table.c`
keyed by the TU's `emitter *` (created on first use, printed and removed by
`vw_stats_print`), so no field is added to `emitimpl.h`.

The pre-IR checker (`src/codegen/vir/preir-check.c`, called only under
`verify`) tests the invariants of Q-VIRB section 2 and returns the first
broken one as a message: value 0 is none; a constant has `blk == VW_NOBLK`
and is in no list; every operand of a live instruction is live or a
constant after `vw_find`; a block parameter has one operand per
predecessor; `V->order` holds reachable blocks only, each once; predecessor
lists are distinct and agree with the successors; every block in
`V->order` has a terminator; from `V->sealing` on every value has a `fwd`
entry.  A failure under `verify` reports `vcc vir-verify: pass=NAME
function=F: MESSAGE` and makes the function fall back (reason
`verifier_rejected`).

`bench/vir-passes.py` (like `bench/record-passes.py`): runs `vpp -O2 -c`
with `VCC_VIR=bl VCC_VIR_STATS=1 VCC_VIR_PASSES=time` over the suites, the
kernel sample (`-n`, default 200) and SQLite, and prints per pass: runs,
changed, instructions (M), share of the VIR compile.  Its line goes into
AGENTS.md.

Pre-IR facts recorded (Q-VIRB-04, the pre-IR half): a third commit makes
the driver keep `needs`/`keeps` as two validity bits in `vwst` (order and
predecessors; dominators), so `vw_doms` and the RPO index run once while
nothing in between cleared them (today `simplify` and `simplify-again`
compute both).  Identical output; instructions must go down (integrator).

Proof adds: with `VCC_VIR=bl VCC_VIR_PASSES=verify`, `bench/vcc-ctests.py
-j 3 --suite vcc --no-cache` passes and reports no `vir-verify` line; for
each optional row, `VCC_VIR=bl VCC_VIR_PASSES=-NAME bench/vcc-ctests.py -j 3
--suite vcc --no-cache` passes; `bench/vir-passes.py -j 3` prints its table
into the PR body.

#### 2c. Analysis validity in `src/vir/` (Q-vir-ir-01, Q-passes-03)

Only `src/vir/` and `src/include/vir/`.  A validity byte `valid` in
`vir_fn` with bits `VIR_VALID_CFG`, `VIR_VALID_DOM`, `VIR_VALID_LOOPS`:

| Who | Does |
|---|---|
| `vir_cfg`, `vir_dom`, `vir_loops_build` | return at once when their bit is set; set it after computing |
| `vir_begin`, `vir_fn_load`, the start of `vir_canon` | clear all bits |
| `vir_canon`'s closing `vir_dom` (`vir.c`, the second call) | becomes a renumbering of `idom` through the old-to-new block map plus one `vir_cfg` (the tree does not change when blocks are renumbered; `pre`/`post` are copied with the block) |
| `vir_alloc` | its `vir_cfg` + `vir_dom` calls return early; its private successor table reads the lists `vir_cfg` keeps, made public in `vir.h` |
| anything that edits a terminator or the block list | clears the bits (today: nothing after `vir_canon`; the arm64 operand rewrite in `vw_a64` changes operands only, check and say so in the PR) |

`cfg_mark`/`cfg_end` move from the thread context into `vir_fn` (they are
per function; a function loaded from a unit forgot them, Q-vir-ir 3).  The
stale "vir_cfg first" notes (`vir.h`, `alloc.h`) go.  A debug check:
`VCC_VIR_CHECK_ANALYSES=1` recomputes each analysis when it would return
early and aborts with `VBUG` (or `abort()` before WORK-common step 3) when
the answer differs.  The verifier (`vir/verify.c`) asks for dominators
through the same entry and stops rewriting the constant table and `f->tmp`
(Q-vir-ir-12) only if `bench/vir-test.py` is unchanged; otherwise that half
is left and noted.

Proof adds: `bench/vir-test.py -j 3` (`dom/`, `canon/`), `bench/vir-alloc.py`
(expected dumps), `python3 bench/virgen-test.py --bin-dir build/u`; the
whole proof list again with `VCC_VIR_CHECK_ANALYSES=1 VCC_VIR=bl`;
instructions must fall (`vbench.py gate`, integrator; Q-passes-03 estimates
1.3 % of a VIR compile).

#### 2d. The lowerings under `src/arch/<cpu>/vir/` (Q-VIRB-03)

Commit 1: `git mv src/codegen/vir/a64.c src/arch/a64/vir/function.c` and
`git mv src/arch/x64/codegen/vir.c src/arch/x64/vir/function.c`; fix their
includes; `src/Makefile`: append `arch/a64/vir/*.c arch/x64/vir/*.c` to
`codegen_SRC` (if the unity build then breaks on the walker's prefix
macros, put them right before `codegen/vir/*.c` instead; `VCC_UNITY=` must
build either way).  Commit 2: the split below (moves; `static` to `VPRIV`
with prototypes in the internal header).  The arm64 table is the review's
cut (`quality-vir-build.md` section 2 and 3.5), by function name:

| New file in `src/arch/a64/vir/` | Functions (today in `codegen/vir/a64.c`) |
|---|---|
| `lowering-internal.h` | `VL_MAXOP`, `vlarg`, `vl`, the thread-local `VLT` declaration, `vlcc`, `vlx`, the `static inline` helpers `vl_isconst` .. `vl_vecacc`, `vl_gx`, `vl_gv`, `vl_fcls`, `vl_ssz`, `vl_once`, prototypes |
| `calling-convention.c` | the ABI section: `vl_assign`, `vl_retreg`, `vl_bigmem`, `vl_fixed`, `vl_regs` |
| `moves.c` | `vl_fail`, `vl_grow`; registers and constants `vl_mat`, `vl_matf`, `vl_ix`, `vl_fx`; moves `vl_rmov`, `vl_qslot`, `vl_reload`, `vl_spill`, `vl_remat`, `vl_probe`, `vl_edits`, `vl_emoves`; `vl_lbl`, `vl_skip`, `vl_next`, `vl_fnext`, `vl_succ`, `vl_plain` |
| `compares.c` | `vl_cmp`, `vl_fcmp`, `vl_chain`, `vl_ccmp`, `vl_cond`, `vl_brcond`, `vl_bc`, `vl_kept`, `vl_once1`, `vl_tfold` |
| `memory.c` | `vl_addr`, `vl_fmem`, `vl_load`, `vl_store`, `vl_maddr`, `vl_mem` |
| `calls.c` | `vl_twice`, `vl_notail`, `vl_pass`, `vl_under`, `vl_sibling`, `vl_call`, `vl_param` |
| `switch.c` | `vl_switch` |
| `address-folds.c` | `vl_pfold`, `vl_cinfo`, `vl_cact`, `vl_cfold` |
| `returns-and-jumps.c` | `vl_chase`, `vl_fcsave`, `vl_ret`, `vl_goto`, `vl_sibcand` |
| `vectors.c` | `VW3`, `VW2`, `vl_vec` |
| `instructions.c` | `vl_ins` |
| `function.c` | `vl_block`, `vl_order`, `vl_body`, `vl_vcross`, `vw_a64` |

x86-64, the same cut (`src/arch/x64/vir/`, from today's
`arch/x64/codegen/vir.c`; its formatting is left as it is, PLAN-quality
decision 6):

| New file in `src/arch/x64/vir/` | Functions |
|---|---|
| `lowering-internal.h` | the state struct `vx` and the definitions above `vx_grow`, prototypes |
| `calling-convention.c` | `vx_arg`, `vx_fixed` |
| `moves.c` | `vx_grow`, `vx_reg`, `vx_size`, `vx_bits`, `vx_raw`, `vx_simd`, `vx_mat`, `vx_memslot`, `vx_edits`, `vx_edge`, `vx_target`, `vx_next`, `vx_goto` |
| `switch.c` | `vx_dense_switch`, `vx_cmpconst`, `vx_switch` |
| `folds.c` | `vx_imm`, `vx_smallmem`, `vx_supported`, `vx_foldedits`, `vx_memnext`, `vx_castmem`, `vx_readword`, `vx_beaddr`, `vx_addrmem`, `vx_clear32`, `vx_sign64`, `vx_memblock`, `vx_condition` |
| `instructions.c` | `vx_ins` |
| `function.c` | `vx_block`, `vx_twice`, `vw_x64` |

Proof adds: `bench/vcc-vir-x86.py`, and CI's x86-64 job (`bench/vcc-x86.py
--suite vcc --objcmp`) green.

#### 2e. One lowering driver (Q-VIRB-02)

`src/codegen/vir/lower-driver.c`: the block driver, the allocator-edit
walk, edge moves, the set-up and the allocation check, written once and
calling a table of target callbacks (`vir_lowering_target`: emit one
instruction, one move, one edit, one edge block, label, jump, finish).
`vl_block`/`vx_block`, `vl_edits`/`vx_edits`, `vl_emoves`/`vx_edge`,
`vl_twice`/`vx_twice`, `vl_grow`/`vx_grow` and the set-up of `vw_a64`/
`vw_x64` become callers of it.  Identical output is the rule: where the two
copies differ today (Q-VIRB-02: the dense-switch count, `noopt` in the
switch test, the trailing edits of a block, `VT_MULTI` operands, the
`VCC_VIR_ROT` caching) the difference becomes a field of the callback table
with today's value per target, and a comment saying it is unresolved.  The
dense-switch difference is already a reported bug (PLAN-quality "Bugs from
the reviews"): fixing it is that bug's PR, not this step.

Proof: as for 2d.

### 3. Close the failure reasons, most frequent first

One PR per sub-step.  These change generated code, so `vcc-same.py` cannot
prove them.  **Proof for every sub-step** is VIR-MIGRATION's "Validation":

```
VCC_VIR=bl bench/vcc-ctests.py -j 3 --no-cache                     # the C suites under qemu
VCC_VIR=bl bench/vcc-ctests.py -j 3 --no-cache --cpu cortex-a53    # without LSE
VCC_VIR=bl bench/vcc-abi.py ; VCC_VIR=bl bench/vcc-abi.py --host   # cross-compiler calls; natively (Apple)
python3 bench/vcc-vir-x86.py --bin-dir build/u                     # x86-64's VIR lowering
VCC_VIR=bl bench/vccd-kbuild.sh work/build-vir -j 3                # when work/ exists, then:
python3 bench/boot-test.py --kernel work/build-vir/arch/arm64/boot/Image --timeout 60
VCC_VIR=bl bench/pg/build.sh vcc                                   # meson test: the same pass set as clang's
VCC_VIR=bl bench/sqlite/run.py all                                 # testrunner: no vcc-only failures
bench/zlib/run.py all                                              # its `vir` build: same bytes, self-tests
bench/vir-failures.py -j 3 [--keep-g]                              # the reason's count is 0, or what is left is listed
```

Plus, needing a quiet machine (the integrator runs them; the PR lists
them under "Proof not run"): instructions per TU on the kernel sample and
SQLite before and after (`VCC_VIR=bl bench/vbench.py ab cc emit --base DIR
--per-tu`); the kernel image's size; `speedtest1` and `zbench` time, no
worse than the direct path.  **Budget:** the first sub-step adds a section
"Budget used" to `doc/quality/VIR-MIGRATION.md`: a table (PR, kernel
sample instructions per TU %, SQLite %, running sum %).  Every sub-step
appends its row; one that would take the sum past 3 % stops and reports to
the owner instead of merging.

**Walker edits.**  When a reason needs the shared walker
(`src/codegen/walk/`) to call something new, the new path is compiled only
in the VIR instance (`#ifdef WK_PREFIX`, as `walk/stmt.c` already does), so
the first instance, x86-64's direct lowering, is unchanged and needs no new
`LO()` function; `wkarg` grows only at its end.  After a walker change run
`src/tools/gen-walkprefix.py` and commit `prefix.h`, and add the x86-64
check `bench/vcc-same.py --base DIR -j 3` (default environment) to the
proof: x86-64's direct output must stay identical.

**x86-64.**  A sub-step closes the reason on arm64.  Where x86-64's VIR
lowering lacks the same support it keeps failing with the same reason, and
the sub-step's "x" twin (one PR, `src/arch/x64/vir/` and nothing else
unless listed) follows.  The x twins touch the same few files, so they run
one after another.

| # | Reason (all sets) | Files it touches (after step 2) | Depends on |
|---|---|---|---|
| 3.0 | `debug_info`: every function with `-g` (G4) | `arch/a64/vir/function.c` (the refusal in `vw_a64`), `arch/a64/vir/instructions.c` (a line row per location change: call `dbg_row`, owned by WORK-codegen-surviving), `codegen/vir/try.c` (Q-VIRB-11: refusals that need no VIR tested before the build) | |
| 3.1 | `inline_assembly` 1 867 (kernel 1 686 of 1 926) | `codegen/vir/lower-control.c` (`vw_asm`: a `VO_ASM`/`VO_ASMGOTO` with the `casm` record as template id), `codegen/vir/to-vir.c`, `codegen/vir/cfg.c` (asm goto targets), `arch/a64/vir/calling-convention.c` (`vl_fixed`: operands in fixed registers, clobbers), `arch/a64/vir/instructions.c` | WORK-codegen-surviving step 9 (the template scanner `codegen/asm-template.c` and arm64's operand printer) |
| 3.2 | `record_argument_or_result` 1 127, with `record_argument`, `record_parameter`, `record_result` | `codegen/vir/lower-call.c` (`vw_classify`, `vw_sig`, `vw_group`, `vw_call`, `vw_params`, `vw_ret`), `codegen/vir/lower-control.c` (`vw_retrec`), `arch/a64/vir/calling-convention.c`, `arch/a64/vir/calls.c`, `arch/a64/vir/function.c`; `src/vir/verify.c` only if a signature group needs a new rule | WORK-codegen-surviving step 8 (AAPCS64 classification as a function of the type, no records) |
| 3.3 | `variadic_function` 212, with `variadic_start`, `variadic_argument` | `codegen/vir/lower-call.c` (`vw_params`), `codegen/vir/lower-control.c` (`vw_va_start`, `vw_va_arg`), `arch/a64/vir/calls.c`, `arch/a64/vir/function.c` (register save areas in the frame) | |
| 3.4 | `wide_integer` 209 (`__int128`), with `bit_precise_integer` | `codegen/vir/lower-value.c` (the stubs `vw_wconst`, `vw_wbin`, `vw_wun`, `vw_wcmp`, `vw_wcvt`, `vw_bigconst` .. `vw_bigcvt`: pairs of i64 operations on the pre-IR), `codegen/vir/lower-memory.c` (`vw_i128mem`, `vw_ldmem`) | |
| 3.5 | `unsupported_instruction` 65 (`acas`, `axchg`) | `arch/a64/vir/instructions.c` (LSE `cas`/`swp`; LL/SC under `--cpu cortex-a53`); proof adds `bench/vcc-atomic.py --target aarch64-linux-gnu` | a record opcode, if one is missing, from WORK-codegen-surviving |
| 3.6 | `unsupported_floating_type` 48 | `codegen/vir/lower-value.c` (`vw_fty` and the libcalls of long double, `_Float16`, `__bf16`, `__float128`), `codegen/vir/lower-call.c` | |
| 3.7 | `stack_pointer` 37 and `alloca` 15: the dynamic stack | `codegen/vir/lower-control.c` (`vw_spget`, `vw_spset`, `vw_alloca`); a new VIR opcode if SPEC-vir has none: `src/vir/table.c`, `include/vir/vir.h`, `vir/verify.c`, `vir/print.c`, `vir/parse.c`, `vir/alloc.c` (frame pointer kept), SPEC-vir 7 and 12, new `test/vir/` cases; `arch/a64/vir/instructions.c`, `arch/a64/vir/function.c` | 2c, if `src/vir/` is touched |
| 3.8 | `switch_bit_set` 27 | `codegen/vir/lower-value.c` (`vw_switch_bitset`: the recipe as `and`/`shl`/`brif` on the pre-IR, as `arch/x64/codegen/switch-bitset.c` does with `LO()`) | |
| 3.9 | `frame_address` 23 | `codegen/vir/lower-control.c` (`vw_frame`: `VO_FRAMEADDR`, `VO_RETADDR`), `arch/a64/vir/instructions.c` | |
| 3.10 | `access_outside_slots` 14 | `codegen/vir/lower-memory.c` (`vw_slotat`), `codegen/vir/promote.c` if promotion must skip the new case | |
| 3.11 | `computed_goto` 9 and `label_address` | `codegen/vir/lower-control.c` (`vw_jumpr` to `VO_IBR`, `vw_named` to `VO_LABELADDR`), `codegen/vir/cfg.c`, `arch/a64/vir/returns-and-jumps.c` | |
| 3.12 | the rest, one PR per reason, most frequent first, from a fresh `bench/vir-failures.py -j 3 --top 40` (today: `over_aligned_slot` in `arch/a64/vir/function.c`, `unsupported_builtin` in `lower-value.c`/`lower-control.c`, eleven reasons at most 8 each in the suites) | as the reason's `VIRFAIL` site says | |

Parallel: two sub-steps run at the same time when their file lists do not
overlap.  First wave: 3.0, 3.2 (once its dependency is in), 3.4, 3.5, 3.9
(`lower-control.c` is shared by 3.1, 3.2, 3.3, 3.7, 3.9, 3.11: one of those
at a time per wave; `lower-value.c` by 3.4, 3.6, 3.8; `instructions.c` by
3.0, 3.1, 3.5, 3.7, 3.9).  The integrator picks the next sub-step from this
table by count and free files.

Each sub-step removes the `VIRFAIL(...)` sites it closes and keeps the
reason in `failure.h` until no site uses it (then deletes it, so a
reappearance is a compile error).  A sub-step that finds the reason hides
another (the function now fails later for a new reason) reports the new
reason and its count in the PR body; that is progress, not a failure.

## Owner's decision: where the optimizer lives

This order does not decide it.  VIR-MIGRATION.md item 4 asks for it
"before step 2"; step 2 above is written so that neither answer is
presumed, which moves the deadline to step 4 (the first pass moved into
VIR).  If the owner wants it before 2a, 2a waits; nothing else does.

**(a) The pre-IR is VIR's mutable form** (Q-VIRB decision 1, the review's
recommendation).  The pre-IR exists because Braun's construction and the
passes need what VIR lacks: insertion into any block (`vw_conv_in` appends
to a predecessor), replacement without rewriting users (`fwd`), deletion.
Under (a) the pre-IR gets a declared header (2a's `preir.h`), the pass
table (2b), a checker (2b's `preir-check.c`), and later an importer VIR ->
pre-IR so that `virtest --pass NAME` runs one pass on a `.vir` file.
Passes moved in step 4 are written on the pre-IR.  SPEC-vir 14 is
rewritten to say so.  Cost: an importer; two forms to know; the copy into
VIR (`to-vir`) and `vir_canon` stay (3.5 % and 5.2 % of a VIR compile of
SQLite, `quality-passes.md` 3.5).

**(b) VIR becomes editable and the passes move onto it** (SPEC-vir 14's
original design).  VIR is dense and append-only (blocks are ranges, values
numbered in block order); it is what is printed, parsed, verified and
allocated.  Under (b) VIR gains an editing layer (insertion, replace all
uses, deletion, then re-canonicalization) and each pass is ported, one row
of the pass table at a time.  Cost: the editing layer and the port; gain:
one form, every pass testable on `.vir` files with no importer, and
possibly no `to-vir` copy.

**(c) Only the CFG passes move to VIR** (`condition-merge`,
`if-conversion`, `split`, block merging), the value passes stay on the
pre-IR.

What step 2 keeps open: one pass per file (2a) is the unit either answer
works on: under (a) the files stay, under (b) or (c) each file is ported
alone and its row in the table switches implementation, with
`VCC_VIR_PASSES` giving the A/B.  The table's `needs`/`keeps` name facts,
not arrays, so it serves both forms.  Analysis validity is recorded on both
sides (2b for the pre-IR, 2c for VIR), so either form reuses its analyses.
Step 3 closes reasons in the builder's `LO()` interface and the lowerings,
not in passes, and depends on neither answer.  What step 2 must not do:
build the importer (only (a) needs it) or an editing API on VIR (only (b)).

Evidence that would decide, all obtainable after 2b:

1. `bench/vir-passes.py` per-pass instructions on SQLite and the kernel
   sample: the share of `to-vir` + `canon` (what (b) could remove) against
   the passes' own share.
2. For each candidate of step 4 (slot forwarding, extensions, compare
   reuse, the division and switch recipes, constant propagation), whether
   it inserts into a block other than the one being built: that is the
   capability only the pre-IR has today.
3. A prototype: port one small pass (`split` or `condition-merge`) to VIR
   behind its table row; compare lines of code, instructions and the
   effort, with `VCC_VIR_PASSES` selecting either.
4. Whether `virtest --pass NAME` on `.vir` files is wanted for every pass
   (it needs the importer under (a), nothing under (b)).

## Not in this order

- **Step 4 of VIR-MIGRATION** (moving passes: the record clean-ups by the
  counters of step 1, then slots, exts and flags, then the walker's recipes
  in `src/codegen/opt/`) and **step 5** (deleting arm64's walker and record
  optimizer, then x86-64's direct lowering): later orders, after the
  owner's decision above.
- The rest of the module pass on `src/vir/` and `src/codegen/vir/`:
  `vir.c` split into nine files (Q-vir-ir-04), the allocator's split
  (Q-vir-ir-07), `parse.c` split (Q-vir-ir-08), per-opcode facts into
  `table.c` (Q-vir-ir-03), one reader of the allocation (Q-vir-ir-02), the
  one unity TU (Q-vir-ir-05), the prefix renames (`virbuild_`, `vir_`,
  Q-VIRB-09, Q-vir-ir-17), opening comments, the knob registry
  (Q-VIRB-10), the target policy record (Q-VIRB-16), dead code (Q-VIRB-18,
  Q-vir-ir-16).  They follow step 4, when the files that survive are known;
  renaming files about to be rewritten would move them twice.
- `src/codegen/walk/`'s own split and renames (Q-CGS-27 etc.): after step 5
  removes its second instance and `walk/prefix.h`.
- arm64's AST walker (`src/arch/a64/codegen/` `expr stmt call arith lval
  value cond fp bind label builtin` and the recipe files), x86-64's direct
  lowering (`src/arch/x64/codegen/lower.c wide.c fp.c` and the record passes)
  and wasm (`src/arch/wasm/`): no edits of any kind from this order.
- Everything owned by `WORK-codegen-surviving.md` (`emitimpl.h`, `tu.c`,
  ABI, inline asm, op tables, printers, encoders, `finish_function`).

## Adoption of common/ facilities

Only after `WORK-common.md` step 5 has merged; one PR per facility, each
proved like step 2 (identical output, both builds, `vcc-same.py` with and
without `VCC_VIR=bl`) plus the ASan suite where an allocation changes.
Counts are from main at d602dfa2 (grep), before step 2 moved the code: find
the sites by the old names.

| File (today) | Sites | Facility | Evidence |
|---|---|---|---|
| `codegen/vir/build.c` | `GROW` 29 | `VVECTOR_NEED`/`VVECTOR_APPEND` | Q-lib-02, Q-VIRB-12 |
| `codegen/vir/vec.inc` | `GROW` 5 | same | Q-lib-02 |
| `codegen/vir/a64.c` | `vl_grow` 14 | same; `vl_grow` deleted | Q-lib-02, Q-VIRB-12 |
| `arch/x64/codegen/vir.c` | `vx_grow` 7 | same; `vx_grow` deleted | Q-lib-02, Q-VIRB-12 |
| `vir/parse.c` | `PV`/`PV_PUSH` 52 | same | Q-lib-02, Q-vir-ir-11 |
| `vir/vir.c` | `UGROW` 4 | same | Q-vir-ir-11 |
| `codegen/vir/build.c` | `malloc`/`calloc`/`realloc` 36, `free` 27 (22 in `vw_simplify`; they leak on `vw_fail`) | a per-function `vregion` reset in `vw_begin` | Q-VIRB-12 |
| `codegen/vir/build.c` | 4 open-addressing tables, the multiplier written 8 times | `vstring_index` where keys are strings, else one table helper in `preir.h` | Q-VIRB-12, Q-lib-03 |
| `vir/vir.c` | the constant table's multiplier 3 | `vhash_*` mix | Q-lib-04 |
| `vir/parse.c` | the name map with FNV-1a (2 constants) | `vstring_index` over `vhash_string` | Q-lib-03, Q-lib-04, Q-vir-ir-11 |
| `vir/parse.c` | `sput` 12 | `vbuffer` | Q-lib-05 |
| `vir/print.c` | `putf` 69 | `vbuffer` formatted append | Q-vir-ir-11 |
| `vir/parse.c` | the hex digit value | `vhex_digit_value` | Q-lib-14 |
| `vir/verify.c`, `vir/vir.c`, `vir/print.c` | list bounds `(uint64_t)off + 1 + n > ne`: 3, 3, 4 (about 20 in all by the review) | `vchecked_range` | Q-vir-ir-11, Q-lib-13 |
| `vir/parse.c` 13, `vir/vir.c` 7, `codegen/vir/a64.c` 2, `arch/x64/codegen/vir.c` 4, `vir/allocchk.c` 2, `vir/allocdump.c` 1 | `abort()` | `vjob_fail` (input, limits), `VBUG` (inconsistency), `vout_of_memory` | Q-lib-01, Q-vir-ir-14 |
| `codegen/vir/build.c` | `vw_fail` (a job-level escape) | stays the builder's fallback; its out-of-memory path calls `vout_of_memory` | Q-lib-01 |
| `bin/virtest/virtest.c`, `bin/viralloc/viralloc.c` | `slurp` 2 + 2 (one function copied) | `vfile_read` | Q-lib-07, Q-vir-ir-10 |
| `bin/viralloc/viralloc.c` | `now()` 4 | `vclock_seconds` | Q-lib-12 |
| `codegen/vir/a64.c` (`VLT`), `codegen/vir/vec.inc` | grow-only thread-local scratch (2 + 2 `_Thread_local`) | job regions, with the scheduler plan's lifetime | Q-lib-19, Q-VIRB-13 |

## Report

Per step: the PR, the proof commands with their results (numbers), what
was not run and why, and what this order got wrong.  Step 3 also: the
reason's count before and after per set, the new reasons it uncovered, its
row in "Budget used".
