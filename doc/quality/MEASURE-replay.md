# MEASURE-replay: what the declaration replay saves and costs (2026-11-13)

## Question

Open decision in `doc/PLAN-quality.md`: "`frontend/c/replay.c`: keep, clean,
or move to a branch. Measure its saving on today's tree first".  Evidence:
`doc/review/quality-cc-cache.md`, finding Q-cc-cache-03 and its section 6,
decision 1 (the review recommends moving it to a branch).

## What is switched, and the default

- `VCC_DR=1` turns replay on (`replay.c` `dr_env`: `dr_mode = getenv("VCC_DR") ? 1 : ...`).
  Unset is off: **the default is off**.  `VCC_DRREC=1` records and publishes but
  applies nothing (counts only).  `VCC_DRCHECK=1` is the checker.
  `VCC_DR_GAIN` (default 10) is the publish gate, `VCC_DR_MAX_MB` (512) the memory cap.
- The prefix snapshot (`driver/ccsnap.c`, `VCC_NODC=1` turns it off) is on in both
  columns below; replay is measured on top of it, which is the only way it can
  run (it is "layer 3").  Header cache: on in both.
- Nothing sets `VCC_DR` in vccd or the kernel build scripts; only five harnesses
  (below) set it.

## Method

Own build of origin/main (6d603396): `nice -n 10 make -C src -j 3 OUT=$PWD/build/mr $PWD/build/mr/vpp`
(unity build, ccache).  Each measurement is one cold `vpp --batch JOBS -j 3 -C DIR`
process (all caches start empty, as in a fresh vccd); the `perf:` line it prints is
the instructions and cycles of all job threads (vperf, `bench/vbench.py`'s counter).
Runs alternate off/on (off, on, on, off, off, on), three each, `VCC_DR` unset or `=1`,
all other `VCC_DR*` unset.  Driver: a throw-away script that builds the jobs file with
the same functions the harnesses use and runs the pair, not committed.

| Set | Jobs file built as | Jobs |
|---|---|---|
| kernel sema | `bench/vcc-sema.py batch -j 3 --kernel` (`-fsyntax-only` + each TU's `.cmd`, `work/build-o0` of the main checkout via `bench/hostenv.py`) | 840 |
| kernel -c | `bench/vcc-cbatch.py` (`-c -o`, same TUs) | 840 |
| PostgreSQL -c | `bench/pg/speed.py`'s `corpus()` over `work/build-pg-clang/compile_commands.json`, `-c`, as `vpp-batch` does | 1574 |

Everything ran: all 840/840 and 1574/1574 TUs ok in every run.
Not run: output identity under `VCC_DR=1` (objects, diagnostics): the premerge
harnesses and RESEARCH-preprocessor.md section 6.9 own that; `vcc-cbatch.py` and
`vcc-sema.py` themselves were not used as programs (they have no off/on switch and
`vcc-sema.py batch` does not print repeated counters), only their job construction.

## Load: timings are indicative only

The rules ask for timing only at 1-minute load under 6.  The machine stayed at
**9.3 to 10.0** for the whole session (uptime at start 9.9, at the end 9.35; per-run
values are 9.25 to 10.05), and it did not drop, so wall and cycle numbers below are
not clean.  Instructions retired move about 0.5 % with load (`bench/vbench.py`), so
they are reliable.  The off and on runs of a pair saw the same load.

## Results (min / median of 3)

Instructions in G, cycles in G, wall = the batch's own wall in s (3 threads).

| Set | Metric | Off min / med | On min / med | Change (med) |
|---|---|---|---|---|
| kernel sema | instructions | 89.29 / 89.51 | 85.60 / 85.70 | -4.3 % |
| | cycles | 28.57 / 28.77 | 28.71 / 28.71 | -0.2 % (min +0.5 %) |
| | wall | 3.157 / 3.212 | 3.229 / 3.249 | +1.2 % (min +2.3 %) |
| kernel -c | instructions | 99.62 / 99.86 | 96.40 / 96.45 | -3.4 % |
| | cycles | 33.51 / 34.16 | 33.78 / 34.54 | +1.1 % (min +0.8 %) |
| | wall | 3.699 / 3.812 | 3.786 / 3.908 | +2.5 % (min +2.4 %) |
| PostgreSQL -c | instructions | 58.93 / 59.23 | 50.61 / 50.64 | -14.5 % |
| | cycles | 18.19 / 18.27 | 17.01 / 17.10 | -6.4 % |
| | wall | 1.992 / 2.008 | 1.863 / 1.907 | -5.0 % (min -6.5 %) |

Load at the runs: kernel sema 9.26 to 9.56, kernel -c 9.36 to 10.05, PostgreSQL 9.32 to 9.37.

What replay did (from its own counters, one run): kernel sema 6286 regions replayed
(99 M tokens), 1415 entries, **514 MB**; PostgreSQL 977 entries, **128 MB**, 41.5 M of
62.5 M parser tokens skipped (kernel: 209 M of 387 M skipped by all decl caches, 111 M
without replay).

Reading: on the kernel, the target of the project, replay removes 3 to 4 % of
instructions and buys no time (cycles and wall are equal or 1 to 2.5 % worse, which is
inside this load's noise, but is not a gain either).  On PostgreSQL it removes 14 % of
instructions and about 6 % of cycles and wall.  RESEARCH-preprocessor.md (layer table at
the top) said 5 to 7 % of instructions and 0 to 1.5 % of cycles; today's kernel
matches that, PostgreSQL's gain is larger than recorded (the tree has since changed;
section 6.10 of that file notes PG's header-cache misses fell; I did not bisect the cause).

## Cost of keeping it

- **`src/frontend/c/replay.c`: 3479 lines**, one TU part of the `cc` unity build
  (about 2.5 % of the tree per the review).  Plus `ccsnap.c` carries three lines for it
  (362-363, 372: keep `dr`/`dr_on` over a restore, `dr_restored`).
- **Hook sites in the parser, today** (all `if (c->dr...)` or a macro that is one
  pointer test when off):
  - `src/frontend/c/func.c:996-1001` (`dr_close`, `drcov_boundary`, `dr_boundary`)
  - `src/frontend/c/tu.c:308` and `:338` (`dr_begin`, `dr_end`)
  - `src/frontend/c/sym.c:235` (`dr_read`: every older-name lookup)
  - `src/frontend/c/type.c:72`, `:111`, `:224` (`dr_intern`, `dr_new`, `dr_func`: the type log)
  - `src/frontend/c/token.c:403`, `:439`, `:448` (`dr_pragma`)
  - `DR_TAGWRITE` at `src/frontend/c/record.c:50`, `:242`, `decl.c:324`, `:347`, `declspec.c:926` (5 sites)
  - declarations `src/frontend/c/ccimpl.h:145-161`; `struct cc` fields
    `src/include/frontend/c/ccint.h:285-289` (`drb`/`ndrb`/`cdrb` are DRCOV, `dr`, `dr_on`
    are replay); `dr_restored` `ccint.h:363`; `cc_dr_stats` `include/frontend/c/cc.h:337`.
  - preprocessor side: `pp_skip_hit` (`src/frontend/pp/setup.c:419`) exists for replay
    (and `pp_skip_top`, shared with the snapshot); `pp_drcov` is set by `VCC_DR`
    (`setup.c:205`) and `drcov_hit`/`drcov_end` (`setup.c:324-372`, `include.c:554`,
    `token.c:176`) are the DRCOV tool the replay's gating shares.
  - about 17 hook sites in 8 files, none shared with a second feature.
- **Harnesses that exist for it**: `bench/vcc-bitint-replay.py` (37 lines),
  `vcc-c2y-cache.py` (43), `vcc-tail-controls.py` (108), `vcc-x86-leaf-frame.py` (239),
  `vcc-x86target.py` (167): 594 lines, five premerge steps (`ci/premerge.sh:88, 94, 99,
  103, 146`), each setting `VCC_DR=1 VCC_DR_GAIN=0` (and `VCC_DRCHECK=1`) to run the
  replay path as one of two modes.  Four of them (x86 and tail-control ones) compare
  replay against no replay for their own feature; `vcc-bitint-replay.py` is
  snapshot-vs-replay only (not checked in detail for the others).  `bench/vbench.py` only names `VCC_NODC` (not replay).
- **Memory**: 514 MB of entries on a kernel build, 128 MB on PostgreSQL, never freed or
  evicted (review Q-cc-cache-21).
- **What a new field or tag write must do** (review section 3): a new field in `sym`,
  `tag`, `field`, `ctype` or `attrs` needs a replay schema row (`*_fd`), plus
  `SYM_REFS`/`FIELD_REFS` if it is a reference, a `REL_LOC` line in `apply` if a
  location, `code_ptrs`/`heap_piece` if a heap pointer, `ty_hash`/`ty_eq` for `ctype`;
  a new node kind or operand needs `node_kinds` and the `MARK` switch; a new array in
  `cc` needs about eight places; any code that writes an older tag needs
  `DR_TAGWRITE`; a new parse option needs a line in replay's hand-picked `opt_sig`
  (it omits 10 options ccsnap's key has).  Nothing enforces these (the checker reads
  the same schemas); the review found 30 fields already missing.

## Recommendation

1. **Remove from main (move to a branch, review option (b))**: on the kernel, the
   project's target, it removes 3 to 4 % of instructions and no measured time, for 3479
   lines, 17 hook sites, five harnesses and 514 MB.  It stays off by default, so removing
   it changes no shipped behaviour.
2. **Not "clean"** (option (a)): the cost is generating its schemas from a field table
   and extending it to every C++ record; that is a large step for 0 % kernel time.
3. What would change it: a clean-load rerun (load under 6) showing the PostgreSQL
   6 % wall gain holds and a release gate or user that builds PostgreSQL-like trees
   (the gain is real there: 14 % of instructions); or the "share a read-only layer
   instead of copying" design (RESEARCH-preprocessor.md, "What is still done once per TU") making the kernel's
   copy cost small.  Then re-evaluate with the field table of Q-cc-cache-02 in place.
