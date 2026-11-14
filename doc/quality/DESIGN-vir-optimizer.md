# Design: VIR's optimizer on VIR itself

**Owner's decision (Johan, 2026-11-13):** "VIR's optimizer lives on VIR
itself: new passes and passes moved from the direct emitters work on the
documented VIR format and are testable with `virtest` on `.vir` files.
Existing pre-IR passes stay until each is ported.  A 2-3 % compile-time loss
on VIR is acceptable."

This note says how: where the passes live, what a pass gets and returns,
how the pipeline runs them, how `virtest` tests them, how they are verified
and timed, and the first pass in full.  It is the design behind steps 4.0
and 4.1 of `doc/quality/WORK-vir.md`; it changes no code.  It was checked
against main at ddbb6413 (after PRs #140, #141, #154, #167, #172 and #175).

## Constraints

- **The direct emitters are not deleted in this wave.**  Nothing here
  removes arm64's walker, its record optimizer or x86-64's direct lowering.
- **Nothing changes at `-O0` or on the direct path.**  VIR passes run only
  inside `vw_emit` (`src/codegen/vir/pipeline.c`), which `vw_try` reaches
  only at `-O2` and up (`doc/OPTIMIZATION.md`).  Every PR from this design
  proves it with `bench/vcc-same.py --base DIR -j 3 --modes c0,c1`
  (byte-identical).
- **A pass on VIR depends on nothing in `frontend/`, `codegen/` or
  `arch/`** (the rule of `src/vir/`, stated in `vir.h`'s opening comment).
  It sees a `vir_fn`, the thread's `vir_ctx` and the pass context below.
  Target facts a later pass may need come through one narrow, data-only
  interface (see "Target facts"); the first pass needs none.
- **The pre-IR passes stay** (`promote`, `trivial`, `dead-params`,
  `simplify`, `condition-merge`, `if-conversion`, `vectorize`, `split`, ...)
  until each is ported, one row of the pass table at a time.
- **Budget:** the 2-3 % covers the whole migration (VIR-MIGRATION.md,
  "Budget used"), shared with step 3 of WORK-vir.  How much of it the VIR
  passes may take is for the owner to decide (Q1 below).  This note proposes
  1 % for all of them together and 0.3 % for the first.

## 1. Where the passes live

```
src/include/vir/pass.h          the pass interface and the registry (public: codegen/vir and virtest include it)
src/vir/pass-list.c             the registry: one row per pass, const; vir_pass_find, vir_pass_list
src/vir/compact.c               vir_function_compact: drop nops and unused constants in place (a utility, not a pass)
src/vir/known-bits.c            vir_known_bits_compute: the analysis the first pass reads
src/vir/pass/NAME.c             one pass per file, the file named for its job in full words
src/test/vir/pass/NAME/*.vir    its tests (`; PASSES: canon,NAME`)
```

- One pass per file; the file name is the pass's table name
  (`src/vir/pass/redundant-extensions.c` for `redundant-extensions`).
- Names follow `doc/STYLE.md` with the module prefix `vir_`.  The entry point
  of pass `redundant-extensions` is `vir_redundant_extensions_remove`, and
  its registry row is `vir_pass_redundant_extensions`.  File-scope helpers
  carry the prefix too (`vir_known_bits_join`, not `join`).
- An **analysis** (a fact about a function that several passes or the
  lowering read: CFG, dominators, use counts, low-32 demand, known bits)
  lives in `src/vir/` next to `vir_cfg` and `vir_dom`, not in `pass/`.
- In `src/Makefile`, the `vir_SRC` line (owned by WORK-vir) gains
  `vir/compact.c vir/known-bits.c vir/pass-list.c $(wildcard vir/pass/*.c)`.
  Everything stays in the `vir` unity TU, so `virtest` (which links
  `$(call mod,vir)`) gets every pass with no other change.  A pass that needs
  the loop forest (`vir/loops.c`, which is in `VIRA_SRC`) also needs
  `VIRA_SRC` linked into `virtest`: one more line in the same Makefile.

## 2. The pass interface

```c
/* src/include/vir/pass.h */
typedef struct vir_pass_context {
	const vir_unit *unit;	/* virtest: the unit the function came from; the compiler: NULL */
	uint32_t budget;	/* SPEC-vir 10.4: rounds a fixed point may take (default 4) */
	uint64_t changes;	/* the pass adds the instructions it rewrote or removed */
	bool checked;		/* -fvir-passes=verify or virtest: the pass may check its own invariants */
} vir_pass_context;

typedef struct vir_pass_info {
	const char *name;	/* stable: -fvir-passes, `; PASSES:`, virtest --passes, the statistics */
	bool (*run)(vir_fn *f, vir_pass_context *ctx);	/* canonical in, canonical out; whether it changed f */
	uint8_t needs;		/* VIR_VALID_*: the analyses it asks for (it calls the getters itself) */
	uint8_t keeps;		/* VIR_VALID_*: still valid after it if they were valid before */
	uint8_t flags;		/* VIR_PASS_TARGET_FACTS: reads vir_target_facts_get (none yet) */
} vir_pass_info;

const vir_pass_info *vir_pass_find(const char *name, size_t n);	/* NULL: no such pass */
const vir_pass_info *vir_pass_list(uint32_t *n);
```

**What a pass gets.**  A function in canonical form (SPEC-vir 2.4) that
`vir_verify` accepts, in the thread's `vir_ctx`.  It may read `f->target`
and `f->fl`, and nothing else about the compilation: no `ecx`, no `cc` and
no symbol names (the compiler passes `unit == NULL`).

**What it returns.**  A refinement of its input (SPEC-vir 10.1 and the table
in 10.2) in canonical form, and `true` if anything changed.  "Canonical out"
is the contract, so that `vir_verify` can run after every pass and a `.vir`
test prints exactly what the pass produced.  A pass that leaves `nop`s or
unused constants calls `vir_function_compact(f)` before it returns.

**Analyses.**  A pass asks for an analysis through its getter at its start:
`vir_cfg(f)` and `vir_dom(f)` (both return at once while `f->valid` has
their bit, from PR #154), `vir_uses(f, cnt)`, `vir_low32(f, ret32, lo32)`
and `vir_known_bits_compute(f, ctx, out)`.  Getters may use the per-block
scratch (`bs[]`), so a pass must not call one while it is itself using that
scratch.  `needs` documents what the pass asks for; it is not a scheduler.

**Invalidation.**  Construction and `vir_canon` already clear `f->valid`.
A pass that changes blocks, edges or terminators calls
`vir_analyses_invalidate(f)` before it returns.  A pass that only rewrites
value operands and turns instructions into `nop`s changes neither blocks
nor edges.  CFG and dominators then stay valid, `vir_function_compact`
leaves `f->valid` as it is (predecessor lists hold block ids; `idom`, `pre`
and `post` do not move), and the row's `keeps` says so.  Checked runs test
that claim: after a pass whose `keeps` has a bit that is still set in
`f->valid`, the runner recomputes that analysis into scratch and compares.
The check is `-fvir-passes=analyses` from WORK-vir 2c (`verify` asks for it
too).  A mismatch is reported as a
failure of that pass, by name.

**Change.**  `run` returns whether it changed the function, and adds to
`ctx->changes` the number of instructions it rewrote or removed.  The
statistics print both: `changed=` counts runs, `changes=` counts
instructions.

**Memory.**  SPEC-vir 22.2 and 22.7 hold: no `malloc`, `mmap` or `free` per
function, and nothing is cleared in proportion to capacity.  A pass's
scratch is the thread's reservations: `f->tmp` (a `uint32_t` per value),
`vir_ctx.aux` (a `uint32_t` per value) and `vir_ctx.bs[0..5]` (a `uint32_t`
per block).  They belong to the running pass between its getter calls and
are undefined when it returns.  A pass that needs more adds one named
reservation to `vir_ctx` (in `virimpl.h` and `vir_ctx_get`); a pass-private
`malloc` fails review.  When WORK-common's job regions are adopted
(WORK-vir, "Adoption of common/ facilities"), these reservations move with
the rest of `vir_ctx`.

**Threads.**  A pass runs on the thread compiling the function, on that
thread's `vir_ctx`.  It has no global mutable state, takes no lock and reads
only `const` tables; the registry is `const` too.  The per-TU counters stay
where PR #175 put them (`pass-table.c`, a mutex-protected table keyed by the
TU's emitter).  The builder's adapter fills them, never the pass.

**Locations.**  `f->loc` (runs of instructions with one source location,
SPEC-vir 22.7) must stay right.  `vir_function_compact` remaps the runs as
`vir_canon` does, and a pass that replaces an instruction with a new one
gives the new one the old one's run.  WORK-vir 3.0 (`-g` on VIR) reads
these runs.

**Budgets.**  SPEC-vir 10.4 allows no fixed point without a budget
(`ctx->budget` rounds).  A pass that runs out of budget falls back to a
sound answer (for the first pass: every late block parameter is "unknown")
and still returns canonical form.

**Failure.**  A pass cannot fail a function; it can only decline to change
it.  If a limit is reached while appending (`vir_failed()`), the adapter
reports `VIRFAIL(build_failed)` and the function falls back to the direct
emitter, as it does today.

### Target facts

The first pass needs no target fact.  When a later pass does (the review's
section 2.5: which integer arguments and results have an unspecified upper
half), it reads a `const` table in `src/vir/target-facts.c`, keyed by
`f->target` (`VTG_*`):

```c
typedef struct vir_target_facts {
	bool i32_args_extended;	/* a caller extends an int argument of <= 4 bytes to 32 bits (Apple: yes) */
	bool i32_upper_free;	/* the upper half of a 32-bit integer result or argument register is unspecified */
	uint8_t pointer_bits;
} vir_target_facts;
const vir_target_facts *vir_target_facts_get(uint32_t target);
```

The table is data only: no callbacks and nothing from `arch/`.  A fact is
added together with the pass that reads it, with a `.vir` test per target.
Whether to allow this interface at all is Q3.

## 3. The pipeline and the pass table

**Where VIR passes run:** after the `canon` row of the builder's table
(`src/codegen/vir/pass-table.c`) and before the lowering (`vw_a64` or
`vw_x64`, called from `vw_try` in `try.c`), on the canonical function in
`V->f`.  Both targets get them, so x86-64's VIR lowering sees their output
and the proof includes `bench/vcc-vir-x86.py`.

**Decision: one table for the user, two lists in the code.**
`-fvir-passes`, the `time` statistics and `bench/vir-passes.py` see one
ordered table: the builder's rows, then the VIR rows.  Pass names are unique
across both.  In the code:

- `src/vir/pass-list.c` is the registry of VIR passes (name, function,
  needs, keeps).  `virtest` and the fuzzers use it, and it has no order.
- `src/codegen/vir/pass-table.c` is the `-O2` pipeline order.  A VIR row
  points at its registry entry and runs through one adapter:

```c
typedef struct virbuild_pass {		/* today's vir_pass of pass-table.h, renamed (STYLE: codegen/vir is virbuild_) */
	const char *name;
	bool (*run)(ecx *e);
	uint8_t needs, makes, keeps, flags;	/* PIR_* and PASS_* as today */
	const vir_pass_info *vir;		/* the registry entry of a VIR pass's row; its run is virbuild_pass_vir_run */
} virbuild_pass;

/* the adapter: the row's VIR pass on V->f */
static bool virbuild_pass_vir_run(ecx *e, const virbuild_pass *p)
{
	vir_pass_context ctx = { .budget = 4, .checked = vir_pass_switch_get()->verify != 0 };
	bool changed = p->vir->run(&e->vw->f, &ctx);

	if (vir_failed())
		vw_fail(e, VIRFAIL(build_failed));
	/* ctx.changes goes to the row's counter with -fvir-passes=time */
	return changed;
}
```

  The rename of `vir_pass`, `vir_pass_list` and `vir_pass_step*` in
  `codegen/vir/` to `virbuild_pass*` is needed anyway (STYLE gives
  `codegen/vir/` the prefix `virbuild_`), and it frees the `vir_pass_`
  names for `src/vir/`.  It is the first commit of step 4.0.

- A VIR row has `needs` 0 and `keeps` every `PIR_*` bit (the pre-IR is
  finished after `to-vir`).  Its flags include `PASS_ON_VIR`, so the
  existing `verify` switch in `pass-table.c` runs `vir_verify` after it.  It
  never has `PASS_CHANGE_UNKNOWN`: a VIR pass always reports change.
- When the switch is read (`vir_switch_read`), every `PASS_ON_VIR` row that
  has a `vir` pointer is checked to carry the same name as its registry
  entry.
- The row masks (`skip` and `verify` in `vir_pass_switch`) and the
  `_Static_assert(... <= 32)` become 64-bit.  There are 19 rows today, and
  each pass being ported adds a row before its pre-IR row goes.

**Porting a pre-IR pass** keeps both rows for a while.  The VIR row is added
after `canon` under its own name (for example `if-conversion-vir`) and is
off by default (a new flag, `PASS_OFF_BY_DEFAULT`; `-fvir-passes` turns it
on with `+NAME` or `only=...`).  `-fvir-passes` then gives the A/B, for
example `-if-conversion,+if-conversion-vir`.  When the VIR row wins on the
proof below, one PR turns it on, deletes the pre-IR row and its file, and
renames the VIR row to the old name.

**`vir_function_compact` instead of a second `vir_canon`.**  `vir_canon`
costs 5.2 % of a VIR compile of SQLite (`quality-passes.md` 3.5); running
it after each pass would use up the whole budget.  A pass that only
rewrites operands and removes instructions needs no reordering and no
dominators.  `vir_function_compact` makes two sweeps in place:

1. mark the constants that are still used;
2. shift the block region over the `nop`s, renumbering operands through
   `tmp`, and remap `blk[].first`, `loc[]` and the constant table (with
   `vir_ctab_rebuild`).

It keeps `f->valid` and costs a fraction of `vir_canon` (to be measured in
step 4.0; the target is at most a third).  A pass that adds blocks, or
instructions in the middle of a block, is the second pass's problem
(section 7); such a pass may end with `vir_canon`.

## 4. `virtest` and the tests

**Running passes.**  `virtest` already reads `; PASSES:` (SPEC-vir 17.1),
but accepts only `canon`.  Step 4.0 makes it look up every other name with
`vir_pass_find` and run the list in order on each function of the unit:

```
virtest [--no-verify] [--bless] [--passes A,B,...] FILE
```

- The list comes from `; PASSES: canon,redundant-extensions` in the file,
  or from `--passes` on the command line, which overrides the file (for
  ad-hoc runs, not for the suite).  A name the registry does not know is an
  error that lists the passes.  `canon` is `vir_canon` and is not in the
  registry.
- After **each** pass, `virtest` runs `vir_verify` (unless `VERIFY: no`) and
  stops at the first failure, printing `@f: after PASS: RULE: message`.
  With `ctx.checked` set, the analysis check from section 2 runs too.
- The result is printed, parsed and printed again (11.1), as today.
- `; CHANGES: yes` appends one comment per function and pass,
  `; pass NAME @f: changed, N instructions` (or `unchanged`), so that a test
  can state that nothing changed, not only what the text looks like.

**Layout.**  Each pass gets `src/test/vir/pass/NAME/`, with `change-*.vir`
(inputs that must change) and `keep-*.vir` (inputs that must not), each with
its expected output after `; ---`.

- `bench/vir-test.py` walks every `.vir` under `src/test/vir`, so it runs
  them with no change.
- `--bless PATTERN` writes the expected part.
- `--mutate N` mutates them like the others, and the mutants now run the
  passes too.  `virtest` must not crash on a verified mutant, and a verified
  function must stay verified after each pass (SPEC 16, `fuzz-vir`).

SPEC-vir 17.2's rule holds: positive cases, plus a negative case for every
"allowed when" condition of 10.2 that the pass relies on.

**SPEC-vir.**  Step 4.0 rewrites three sections:

- section 14: what exists (the builder's pre-IR passes as listed in the pass
  table, the VIR passes of the registry), and that 14.2-14.5 are targets,
  not code;
- section 16: `virtest --passes`;
- section 17.1: `CHANGES:` and the `pass/` directory.

## 5. Verification and measurement

**Checked runs.**  `-fvir-passes=verify` (PR #175) already runs
`vir_verify` after every `PASS_ON_VIR` row.  A function that fails falls
back with `verifier_rejected`, and the compiler prints
`vcc vir-verify: pass=NAME function=F: ...`.  VIR rows inherit this, and the
sanitizer builds run the suites with it (below).  The verifier is never on
the normal path (SPEC-vir 22.7).

**Proof for a pass that changes output** (VIR-MIGRATION, "Validation";
identical output cannot prove it):

```
bench/vcc-same.py --base DIR -j 3 --modes c0,c1                     # -O0 and the direct path (-O1) byte-identical
bench/vcc-ctests.py -j 3 --no-cache -O3 --vflags -fvir-passes=verify   # all C suites under qemu, no vir-verify line
bench/vcc-ctests.py -j 3 --no-cache -O3 --cpu cortex-a53
bench/vcc-ctests.py -j 3 --no-cache -O3 --suite inline
bench/vcc-abi.py ; bench/vcc-abi.py --host                          # at -O2: through VIR
python3 bench/vcc-vir-x86.py --bin-dir build/u                      # x86-64's VIR lowering sees the pass's output
bench/vir-test.py -j 3 ; bench/vir-test.py --mutate 3000 --bin-dir build/src-asan
bench/vcc-ctests.py -j 3 -O3 --vflags -fvir-passes=verify --suite vcc --bin-dir build/src-asan
bench/vcc-tsvc.py                                                   # if work/testsuites/TSVC_2 is fetched: 151 checksums, vectorized count
bench/zlib/run.py all ; bench/sqlite/run.py all                     # same bytes; testrunner: no vcc-only failures
bench/pg/build.sh vcc                                               # when work/ is usable
bench/record-passes.py -j 3 -n 200                                  # the counters the pass should move (section 6)
bench/vir-passes.py -j 3                                            # the pass's own row: runs, changed, instructions, share
```

Some measurements need a quiet machine.  The integrator runs them, and the
PR lists them under "Proof not run":

- `bench/vbench.py ab cc emit --base DIR --per-tu` on the kernel
  sample and SQLite (their `-O2`: through VIR);
- `bench/vbench.py gate`;
- `speedtest1` and `zbench` run time.

**Compile-time budget.**  Measured in instructions retired, two ways:

1. **The pass's own cost:** its row in `bench/vir-passes.py` (`-O3
   -fvir-stats -fvir-passes=time`), as a share of `@build` +
   `@lower`, on SQLite and the kernel sample.  The first pass's budget is
   at most 0.3 %.
2. **The net effect**, which can be smaller or negative (fewer records to
   allocate, lower and clean up): `bench/vbench.py ab cc emit
   --base DIR --per-tu` (the TUs' `-O2`: through VIR).  This percentage is the pass's row in
   VIR-MIGRATION.md's "Budget used" table, whose running sum must stay
   under the owner's 3 %.

**CI.**  premerge runs the C suites at `-O2` (VIR), `-O1` (the direct
emitter) and `-O3 -fvir-verify -fvir-check` (`ci/premerge.sh arm64` runs
`ctests`, `ctests-O1` and `ctests-O3`, plus `vir-x86` on x86-64 hosts), so
VIR is in the gate.  The pass verifier is not: that needs
`--vflags -fvir-passes=verify` in a step of the arm64 job.
`ci/premerge.sh` is not WORK-vir's file: see Q4.

## 6. The first pass: `redundant-extensions`

### Why this one

The candidates come from WORK-vir's counters (PR #140: records changed by
each arm64 clean-up after VIR's lowering, as suites / kernel / SQLite) and
from VIR-MIGRATION's table of what happens to each pass:

| Candidate | Counter on VIR output | Fate (VIR-MIGRATION) | On VIR | Risk |
|---|---|---|---|---|
| redundant extensions | `exts` 5 479 / 3 102 / 1 576 | moves to VIR | one analysis and operand rewrites; no insertion, no CFG edit | low: pure ops, a clear refinement rule |
| copies | `copies_1` 3 430 / 987 / 2 457 | an allocator or edge-move fix | not a pass (SSA has no copies) | |
| repeated compares | `flags` 1 236 / 138 / 245 | moves (value numbering) | the builder's `simplify` already numbers values over dominators | small gain |
| dead results | `dead` 197 / 322 / 2 639 | expected to disappear | mostly leftovers of the lowering | small gain |
| `fold`, `tail_expand`, `fallthrough`, `frame_rewrite` | large | stay in the lowering | not passes | |
| if-conversion, condition-merge, split (pre-IR) | (output identical if ported faithfully) | port | needs block edits and insertion | medium-high |
| division by a constant | (a walker recipe) | VIR rewrite | needs insertion and a walker change | high |

`exts` is the largest counter whose fate is "moves to VIR".  For this note,
what it removes was measured with a local, uncommitted counter in `exts`
(`vpp -O3 -S`), by the kind of record it finds redundant:

| Record found redundant | 1 500 suite TUs | SQLite (`b-gcc/sqlite3.c`) | Where it belongs |
|---|---|---|---|
| `mov wD, wS` (VIR `ext.4u`, i32 to i64) | 2 474 (59 %) | 866 (55 %) | **the lowering**: on arm64 a 32-bit instruction already zeroes the upper half |
| `and xD, xS, #2^k-1` (VIR `and` with a low mask) | 1 176 (28 %) | 74 (5 %) | **VIR**: the operand provably fits in k bits |
| `uxtb`/`uxth` (VIR `ext.1u`/`ext.2u`) | 301 (7 %) | 458 (29 %) | VIR when the type stays the same (i32 of i32); otherwise widening, see R4 |
| `sxtw` (VIR `ext.4s`) | 206 (5 %) | 160 (10 %) | the lowering (the register is already sign-extended) |
| other `sxtb`/`sxth`, 32-bit `and` | 13 | 39 | VIR |

So about 35 % of what `exts` does on VIR output (1 490 of 4 170 in the
suites; 571 of 1 597 on SQLite, which matches #140's 1 576) is a fact about
VIR values: "this value already fits".  The builder's pre-IR rules
(`vw_zw` and `vw_sw` in `codegen/vir/rules.c`) only look at the defining
instruction.  What remains needs facts carried through block parameters and
through `and`, `or`, shifts, `add` and loops, which the record pass gets
from its forward data flow over registers.  The other ~60 % comes from
arm64's register model; it belongs to the lowering, not to a VIR pass (Q5).

### Algorithm

**Analysis.**  `vir_known_bits_compute(f, ctx, out)` (`src/vir/known-bits.c`)
computes two small numbers for every integer value `v` (type `i32` or `i64`,
of width `w` = 32 or 64), packed into `out[v]` (`f->tmp`):

- `z(v)`: `v` < 2^z as an unsigned `w`-bit number (`z = w` means nothing is
  known);
- `s(v)`: `v` is the sign extension of its low `s` bits (`s = w` means
  nothing is known).

Transfer, in canonical order (definitions before uses):

| Instruction | `z` | `s` |
|---|---|---|
| `iconst k` | bits of k | signed bits of k |
| `load.W` (not `MS`), width 2^W bytes < the type | 8·2^W | 8·2^W + 1 |
| `load.Ws` | w | 8·2^W |
| `ext.Wu` | min(8·2^W, z(a)) | that + 1 |
| `ext.Ws` | z(a) if z(a) < 8·2^W, else w | min(8·2^W, s(a)) |
| `trunc` | min(z(a), 32) | s(a) if s(a) ≤ 32, else 32 |
| `icmp`, `fcmp` | 1 | 2 |
| `and a, b` | min(z(a), z(b)) | max(s(a), s(b)) |
| `or`, `xor` | max(z(a), z(b)) | max(s(a), s(b)) |
| `shl a, k` (constant k < w) | min(w, z(a) + k) | min(w, s(a) + k) |
| `lshr a, k` | z(a) − k if z(a) < w, else w − k (at least 0) | z + 1 |
| `ashr a, k` | z(a) − k if z(a) < w | max(1, s(a) − k) |
| `add a, b` | max(z(a), z(b)) + 1, capped at w | max(s(a), s(b)) + 1, capped |
| `sub a, b` | w | max(s(a), s(b)) + 1, capped |
| `mul a, b` | min(w, z(a) + z(b)) | w |
| `udiv a, b` | z(a) | z(a) + 1 |
| `urem a, b` | min(z(a), z(b)) | that + 1 |
| `clz`, `ctz`, `popcnt` | 7 | 8 |
| `select c, a, b` | max(z(a), z(b)) | max(s(a), s(b)) |
| `bparam` | max over the `br` arguments into it | max over them |
| anything else (`param`, `call`/`proj`, `p2i`, atomics, `va_arg`, `asm`, ...) | w | w |

After each row:

- `z` and `s` are capped at `w`;
- `s ≤ z + 1` when `z < w` (a value below 2^z is a signed (z+1)-bit value);
- a shift count is the constant's value, and a non-constant count gives
  "nothing known".

Every entry is a claim about every execution in which the instruction has
no immediate UB.  UB flags (`nsw`, `nuw`, `exact`) are not used, so no rule
depends on them.

**Loops.**  Block parameters start optimistic (`z = 0, s = 1`: no value
yet), and their facts only ever fall.

- A parameter is **late** if its block has a predecessor at or after it in
  the block array (the edges `vir/loops.c` calls back edges).
- One round is a forward sweep over all blocks.  Rounds repeat while some
  late parameter's facts fell, up to `ctx->budget` rounds (4).
- If the budget runs out, every late parameter is set to "nothing known"
  (`z = s = w`) and one more round runs.  Every remaining parameter then has
  only earlier predecessors, so the result is sound (SPEC-vir 10.4's budget
  rule).

Cost: one sweep for code without loops, typically two with them.

**Rewrite.**  `vir_redundant_extensions_remove(f, ctx)`
(`src/vir/pass/redundant-extensions.c`):

0. A first scan counts the candidates: `ext`, and `and` whose second operand
   is an `iconst` of value 2^k − 1.  If there are none, return `false`.
   Most functions stop here; `exts` makes the same early exit.
1. Compute the known bits.
2. Make one forward sweep with a replacement map `rep` in `vir_ctx.aux`.
   `rep[v]` is written when `v` is visited, so nothing needs clearing.  For
   each instruction, first rewrite every value operand `p` to `rep[p]`
   (including the lists of `br`, `call`, `ret` and `asm`, through
   `vir_oit`).  Then decide for the instruction itself:
   - **R1:** `ext.Wu` whose result type equals its operand's type and with
     `z(a) ≤ 8·2^W`: `rep[i] = a`.
   - **R2:** `ext.Ws` whose result type equals its operand's type and with
     `s(a) ≤ 8·2^W` or `z(a) < 8·2^W`: `rep[i] = a`.
   - **R3:** `and a, k`, where `k` is an `iconst` with every bit below
     `z(a)` set (`k & (2^z(a) − 1) == 2^z(a) − 1`): `rep[i] = a`.
   - **R4** (canonicalization, optional; see "Expected effect"): `ext.Wu`
     from `i32` to `i64` with W < 2 and `z(a) ≤ 8·2^W` is rewritten in place
     to `ext.2u` (the plain widening); likewise `ext.Ws` with W < 2 and
     `s(a) ≤ 8·2^W` becomes `ext.2s`.  Same value, no new instruction.
   - Otherwise `rep[i] = i`.

   An instruction with `rep[i] != i` becomes a `nop`: it is pure, and it is
   now unused because all its uses come later and were rewritten.
   `ctx->changes` counts it.  Because `rep[a]` was settled before `i`,
   chains (an `ext` of an `and` of a load) collapse in a single sweep.
3. If anything became a `nop`, call `vir_function_compact(f)`.  It drops the
   `nop`s and any constant only they used (such as the mask), keeps the
   first-use order and remaps `loc`.  Return whether step 2 changed
   anything.

**Preconditions.**

- The input is canonical and verified (definitions before uses).
- Integer types only; vectors, floats and pointers are left alone.
- `keeps` is CFG and dominators: no block, edge or terminator changes.
- No target fact is used.
- Volatile loads are not touched: only their values are compared, and the
  load itself stays.

**Refinement** (SPEC-vir 10.2):

- R1-R3 replace an instruction whose effects are P with an earlier value
  that equals it on every execution (the facts are upper bounds valid on all
  paths).  The replacement value dominates every use of the old one, because
  it dominates the old one.
- R4 replaces an instruction with one that has the same value.
- Removing the instruction that became a `nop` is "remove an instruction
  whose result is unused", with effects P.

### Tests (`src/test/vir/pass/redundant-extensions/`)

Each file starts with `; PASSES: canon,redundant-extensions` and
`; CHANGES: yes`.

Must change:

| File | Input | Expected |
|---|---|---|
| `change-and-after-narrow-load.vir` | `v = load.1u p`; `and v, 255` | the `and` and the constant 255 are gone |
| `change-ext-through-parameter.vir` | two predecessors pass `load.1u` values to `b3`'s parameter; `ext.1u` of it in `b3` | the `ext` is gone (join) |
| `change-loop-counter.vir` | `i` = parameter (0, then `and (add i, 1), 255`); `ext.1u i` in the body | the `ext` is gone (loop fixed point: z = 8) |
| `change-ext-of-ext.vir` | `ext.2u (ext.1u x)` | the outer one is gone |
| `change-shift-fits.vir` | `x = lshr y, 25` (i32); `ext.1s x` | gone (z = 7 < 8) |
| `change-chain.vir` | `and (and (load.1u p), 255), 255` | both gone in one run |
| `change-widen.vir` (R4) | `x:i32 = load.1u`; `y:i64 = ext.1u x` | `ext.2u x` |
| `change-br-list.vir` | the replaced value is a `br` argument and a `call` argument | the lists name the operand |

Must not change (`; pass ... unchanged`):

| File | Why not |
|---|---|
| `keep-param.vir` | `ext.1u` of a `param`: nothing known |
| `keep-call-result.vir` | `ext.1u` of a call's result |
| `keep-mask-too-narrow.vir` | `and (load.2u p), 255` |
| `keep-sign-vs-zero.vir` | `ext.1u (load.1s p)` and `ext.1s (load.1u p)`: the bits differ |
| `keep-sub.vir` | `ext.1s (sub 0, x)` |
| `keep-loop-grows.vir` | the parameter is `shl i, 1` around the loop: the fixed point reaches "nothing known" |
| `keep-budget.vir` | nested loops that need more than 4 rounds: late parameters fall to unknown and the `ext` stays |
| `keep-type-change.vir` | `ext.2u` from i32 to i64 (the type changes; that case belongs to the lowering) |
| `keep-vector-and-float.vir` | a vector `and`, an `fext` |
| `keep-nothing-to-do.vir` | no candidate: the early exit, output identical |

`bad/` stays as it is, and `--mutate` covers the new units.

### Expected effect

- **`exts` counter** (`bench/record-passes.py -j 3 -n 200`): on the VIR path
  it falls by the share R1-R3 cover.  That is at most about a third on the
  suites and SQLite (the `and` and same-type `uxtb`/`uxth` rows above), and
  probably less on the kernel (not measured: `work/build-o0` was not used for
  this note).  `copies_2` and `dead` may fall a little (fewer `mov`s left
  behind).
- **R4** moves records from the `uxtb` row into the `mov w` row.  That only
  pays off once the lowering rule of Q5 exists.  If R4 does not lower
  `exts`, it is left out of the PR (measure with and without it).
- **Pass cost** (`bench/vir-passes.py`): one new row, `redundant-extensions`,
  under 0.3 % of `@build` + `@lower` on SQLite and the kernel sample.
- **Instructions per TU** (`vbench.py ab cc emit`): about flat; the fewer
  records afterwards may pay for the pass.
- **Run time:** `speedtest1`, `zbench` and TSVC no worse (a removed `and` or
  `uxtb` in a loop is a small gain).

**Kill criterion** (Q2): if `exts` on VIR output falls by less than 15 % on
both the suites and SQLite, the PR reports that and the owner decides
whether the pass stays.

### Proof

This pass **changes generated code at `-O2` with VIR**, on arm64 and on
x86-64.  The proof is therefore section 5's list, not byte identity:

- the C suites with `-O3 --vflags -fvir-passes=verify`: no `vir-verify`
  line and no new failure, including the gcc-torture, tcc and inline suites;
- `--cpu cortex-a53`, the ABI checks and `bench/vcc-vir-x86.py`;
- `bench/vcc-tsvc.py` when TSVC is fetched, zlib's same bytes and SQLite's
  testrunner;
- `bench/vir-test.py` with the new tests, and `--mutate` under ASan;
- the counters.

Two byte-identity checks still apply:

- `bench/vcc-same.py --base DIR --modes c0,c1` is byte-identical
  (nothing at `-O0` or on the direct path moved);
- VIR's output with `-fvir-passes=-redundant-extensions` is byte-identical
  to the base's VIR output too (`bench/vcc-same.py --base DIR --modes c2,s3
  --vflags -fvir-passes=-redundant-extensions`): with the pass off, VIR output is what it was
  before, so step 4.0's infrastructure changed nothing.

### What could go wrong

- **Adjacency patterns in the lowering** can stop matching because an
  instruction in the pattern disappeared.  `vir_read_be` and
  `vir_sext_shift` (`vir/facts.c`) and the address folds
  (`arch/a64/vir/address-folds.c`) match consecutive indices.  R1-R3 only
  remove same-type `ext` and `and`, which those patterns do not contain, but
  compaction shifts indices.  The zlib bytes and the SQLite run check this.
- **The lowering's `lo32`** (`vir_low32`) sees different uses.  It can only
  gain, since there are fewer readers.

## 7. The second pass

**`if-conversion` on VIR**, ported from the pre-IR
(`src/codegen/vir/if-conversion.c`), behind its own row
`if-conversion-vir`, off by default until it wins the A/B.  Why it is
second:

- **It is the review's open decision 1** (`REVIEW-vir-lowering.md` 2.4).
  On the pre-IR it runs after the only simplifier, visits outer diamonds
  before inner ones, has no profitability test and cannot be tested by
  `virtest`.  On VIR it gets `.vir` tests and runs before a simplification.
- **It brings the editing layer.**  It is the first pass that edits the CFG
  and inserts instructions (a `select` where a diamond was).  The later
  passes (division by a constant, switch lowering, slot forwarding) need the
  same layer: a copying rebuild into the second instruction array
  (`vir_ctx.ins[cur ^ 1]`, the array `vir_canon` copies into), with
  insertion, block removal and the replacement map, ending in `vir_canon`.
  That layer gets its own short design in the PR before any code (Q6).
- **The A/B is cheap and exact:** `-fvir-passes=-if-conversion,+if-conversion-vir`
  against today, with the proof of section 5.

The third pass, VIR-MIGRATION.md's trial of the walker's recipes, is
**division by a constant** as a VIR rewrite.  The walker's VIR instance
then emits `sdiv x, K`, and the pass also catches divisors that become
constant only after promotion.

## 8. Questions for the owner

1. **Budget split.**  The 2-3 % covers the whole migration.  Proposed: all
   VIR passes together take at most 1 % of a VIR compile (kernel sample,
   SQLite), the first at most 0.3 %, and step 3's lowering work takes the
   rest.  Accept, or set other numbers.
2. **Kill criterion** for `redundant-extensions`: keep it only if `exts` on
   VIR output falls by at least 15 % on the suites and SQLite (or run time
   improves measurably)?
3. **Target facts in `src/vir/`.**  Allow the data-only table of section 2
   (`vir_target_facts_get`, keyed by `VTG_*`) for passes that need ABI
   facts, or require every such pass to stay in the lowering?
4. **CI.**  Add `bench/vcc-ctests.py -O3 --vflags -fvir-passes=verify
   --suite vcc` to `ci/premerge.sh`'s arm64 job (and the asan job) along
   with the first pass?  `ci/premerge.sh` is not in WORK-vir's file list.
5. **The lowering half of `exts`** (about 57 %: `mov w` for `ext.4u` of a
   value defined by an arm64 32-bit instruction, and `sxtw` of a register
   that is already sign-extended).  This is an instruction-selection rule in
   `src/arch/a64/vir/instructions.c` ("fold stays in the lowering",
   VIR-MIGRATION).  Should it be scheduled as a lowering step of WORK-vir
   (it changes output; proof as in section 5), and when?
6. **The editing layer** the second pass introduces (insertion and CFG
   edits through a copying rebuild in `src/vir/`): accept it as the single
   way passes insert, so that no pass grows its own?
7. **SPEC-vir 14's pass list** (`simplify`, `gvn`, `mem`, `dce` on VIR)
   exists today only as pre-IR passes in the builder.  Does "each is ported"
   mean all of them (the end state has no pre-IR optimizer, and the pre-IR
   is only the builder's construction form), or only those that gain from
   `.vir` tests or a second target?
