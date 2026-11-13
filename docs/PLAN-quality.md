# Code quality: how the plan gets written

**Goal:** before the C++ front end starts, make the toolchain's source sharp:
no duplicated code, shared patterns in one place, one scheduler that scales
with the machine, one pass per file with no repeated tree walks, and names
(files, functions, variables) that say what things do.

**This document is the method, not the plan.**  It says how the plan is
produced (stages P0-P4), what evidence it must rest on, and which decisions
are needed first.  P3 turns this file into the plan proper: workstreams,
steps, verification and status per step, as the other `PLAN-*.md` files.

Paths below are today's (`ccs/vcc/...`).  The work starts after the tree
reorganisation (PR #134, `PLAN-tree.md`) is on main; `retarget-paths.py text`
rewrites the paths here.

## Principles

1. **A sequence of behaviour-preserving steps, not a rewrite.**  The default
   rule for every step is PLAN-tree's: outputs byte for byte
   (`bench/vcc-same.py --base`), `bench/vbench.py gate` within budget, unity
   and file-by-file builds, premerge green.  A step that is meant to change
   output or speed says so and is reviewed as such.  The test wall (15 k
   outputs, kernel boot, PostgreSQL, SQLite) is what lets a restructuring this
   large stay safe; a rewrite would throw that away.
2. **Measured.**  "Sharp" is a set of numbers a script prints (P0).  Each step
   names the number it moves; CI later refuses a regression (a ratchet).
3. **Target before tasks.**  The plan has a target architecture (pipeline,
   scheduler, shared library, naming rules) decided before the step list.
   Splitting and renaming toward an undecided target moves every file twice.
4. **Audited, not sampled.**  The plan rests on reviews that read every file
   of their area in full.  The survey below read excerpts; it gives leads.
5. **Move and change apart.**  A commit either moves/renames (`git mv`,
   scripted) or changes code, never both, so reviews and merges stay possible
   with 130 open branches.
6. **Compile speed is a quality.**  Fewer walks and one scheduler should lower
   instruction counts; no step trades the 2 s goal for tidiness.

## First survey (2026-11-13, main at 2e6d3e73): leads, not findings

160 k lines in 277 files outside tests and generated tables.

| Area | What the survey saw | Checked by grep |
|---|---|---|
| Code generators | Three AST walkers that generate code: arm64's own (`arch/a64/emit/`, 14 k lines), the shared `emit/walk/` (x86-64 and VIR), wasm's (`arch/wasm/emit/w*.c`).  Switch lowering, compound assignment, atomics, struct copy, constant folding, bit-field pieces and inline-asm template expansion exist two or three times, often the same code with other call names (`bf_piece` = `wk_bf_piece`) | division/switch recipe files: 15 files for 4 recipes |
| Linkers | `ld.c` (6.7 k lines), `wasmld.c`, `ldmacho.c`, `objcopy.c`: three symbol tables (`gfind`), two `map_file`, four `!<arch>` readers, private byte and output-buffer helpers, argv by hand in each | |
| Small helpers | FNV-1a written inline; `now()`; LEB128 encoders (6); `die`/`err`/`xmalloc` variants; `write_file` (3); private vector macros (7) while `lib/vec.h` has one user; `lib/mapfile.h` has one user | FNV in 17 files, `now()` 6, `realloc(` 111, `mmap(` 29 |
| Threads | Six private mechanisms, no shared pool: vccd workers (2 x all cores), emit workers per TU (`VCC_THREADS`, default 1), vlink `pfor` (P cores on macOS, lazily started, never joined), lexer I/O (3, the only ones with a QoS class), `vpp --batch` (hard-coded 6), lextest.  Three mutex+condvar queues, two atomic-index parallel-fors.  Pools nest without a common cap (vccd x emit, vccd + vlink).  `PLAN-fas2.md:160-173` describes one pool with work stealing; the code is not that | 6 `pthread_create` sites; the counts and sources as stated |
| Passes | Front end: a function body is walked 3-5 times after parsing (two reachability walkers, `cc/flow.c` and `cc/func.c`; uninit; instrument; two linear scans).  No visitor: ~70 hand-written `switch (x->op)` walkers.  arm64: up to 3 AST walks (retry on temporaries), then 8-10 sweeps over records in `opt.c`.  VIR: 15-20 sweeps over the pre-IR in `build.c`, then arm64's record clean-ups again.  Seven or more "has side effects / is pure" predicates | |
| Files with many jobs | `ld/ld.c`, `as/as.c` (4.1 k), `emit/vir/build.c` (4.0 k: SSA construction, lowering interface, GVN, dominators, if-conversion, emission, vectorizer by `#include`), `vccd/vccd.c`, `driver/ppdriver.c` (`ppd_run1`: 1066 lines), `arch/a64/emit/opt.c`.  18 functions over 300 lines | |
| Names | File names that hide their job (`hcentry.c`, `replay.c` with three meanings across modules, `exprop.c`, `tu.c`, `ppdriver.c`, `ccsnap.c`, `rsh.c`, `w*.c`); most arm64 emitter functions unprefixed (`mem`, `slot`, `treg`); state named `E->mf`, `E->K`, `W.la`, `VLT.chs`; `W` names three structs | |
| Documents | No document traces the pipeline end to end.  Stale headers: `emit/emit.h`, `obj/obj.h`, `as/as.h` say arm64 only; AGENTS.md says `vir/` is linked into no tool.  No duplicate detection or lint in `bench/` or `ci/` | 58 distinct `getenv` knobs |

## Lenses

Your six, and four more that the survey says belong with them:

| | Lens | Question the review answers |
|---|---|---|
| L1 | Duplication | Which code exists more than once, exactly or under other names, and where does the one copy live? |
| L2 | Shared patterns | Which idioms (vectors, hashes, buffers, file mapping, byte I/O, LEB, option parsing, fatal errors) should be one facility in `common/`, and which `lib/` variants merge (TODO.md "Variants not yet merged")? |
| L3 | Scheduling | One task scheduler for compile jobs, per-function emission, link phases and I/O: thread count from the machine (P/E cores, QoS, `-j`), no nesting, per-thread state with a defined lifetime |
| L4 | Passes | What is each pass, what does it read and produce, which file holds it, and which walks can be fused or share an analysis? |
| L5 | File names | Does the name say the job?  One meaning per word across modules |
| L6 | Identifiers | Prefix rule per module, no single-letter long-lived state, a glossary; each module opens with what it does and its invariants |
| L7 | Module boundaries | Who may include whom; what is public; dependency direction checked by a script (what C++ needs from `cc/ccint.h` belongs here) |
| L8 | Global state | Single-instance globals that force a lock (`ldlock`, Mach-O link, objcopy), 80 thread-locals without teardown, 58 environment knobs: which are product options, which debug, which dead |
| L9 | Errors and limits | One policy for fatal errors, out-of-memory and untrusted input, instead of one `die`/`err` per file |
| L10 | Dead weight | Rejected experiments, fallbacks nothing uses, stale comments and plan text |

## Stages

| Stage | Output | How |
|---|---|---|
| **P0** Baseline | `bench/quality.py` and its numbers in Status below | A report script: token-level clone scan on vcc's own lexer (normalised identifiers, windows of N tokens, so renamed copies show), file and function sizes, counts of the private helpers above, thread mechanisms, environment knobs, files without a header comment, includes across modules.  Under 10 s, checkout only, so it can become a premerge ratchet |
| **P1** Reviews | `docs/review/quality-AREA.md`, one per area | One read-only session per area, in parallel, each reading its files in full and filling the template below.  Areas: (1) lex, pp, header cache; (2) diagnostics; (3) C front end; (4) shared codegen: `tu`, prepass, `walk`, `rec`, `opt`, `flow`; (5) the three walkers as one concept matrix: arm64, x86-64, wasm; (6) record optimizers and printers per target; (7) VIR: `vir/`, the builder, both lowerings; (8) assembler, `obj`, object writers; (9) linkers and binutils; (10) driver, server, shim, `rsh`, `ar`; (11) `lib/`.  Cross-cutting: (A) scheduling, with a measurement of idle and oversubscribed time in a kernel build; (B) traversal cost per pass in instructions (`vperf`), so fusing is ranked by what it saves |
| **P2** Target | `docs/ARCHITECTURE.md`, `docs/STYLE.md` | ARCHITECTURE: the pipeline as it is (missing today, and cheap), then as it should be: pass list with inputs/outputs, the analysis each pass may reuse, the scheduler, the shared library, the module dependency graph.  STYLE: file-name rules, prefix per module, header-comment template, glossary.  The decisions below are taken here, by measurement where they depend on speed |
| **P3** Plan | this file rewritten | Findings grouped into workstreams; each step has what, files, verification, the number it moves, and what it depends on.  Order: shared library first (everything else uses it), then splits (moves only), then renames in one window per module, then the unifications (walkers, linkers, scheduler, pass fusion), each behind the gates |
| **P4** Proof | one pilot step per workstream, done | Calibrates effort and shows the gates catch mistakes (for example: FNV to `common/hash.h`; one pass out of `opt.c`; one module renamed by script; `vpp --batch` on the new scheduler).  A fresh reviewer then attacks the order; after that execution starts |

### Review template (P1)

Each finding: an id (`Q-ld-07`), lens, evidence as `file:line` on both sides,
class (duplicate, misplaced, misnamed, redundant walk, mixed file, global
state, dead), proposed home, size (S/M/L), risk, and the harness that proves
the fix.  Each review ends with "Decisions needed" and "Not covered", as
`REVIEW-vir-lowering.md`.

## Decisions needed before P2 ends

1. **arm64 onto the shared walker.**  The largest duplication by far, already
   in TODO.md.  It is the -O0 kernel path, so the cost of `LO()` and records
   must be measured first.  Proposal: in scope, its own workstream, ordered
   last, decided by a pilot on one statement family.
2. **wasm onto the shared walker**, or a documented exception (structured
   control flow is the real difference).
3. **One linker core** under ELF, Mach-O and wasm (symbols, archives, files,
   options), or three linkers on one `common/`.
4. **Pass framework.**  A pass table with declared analyses for records and
   VIR's pre-IR, and one fused flow walk in the front end, against today's
   hand-called sequences.
5. **Scheduler shape.**  `PLAN-fas2.md`'s single pool with work stealing, or a
   simpler shared parallel-for plus job queue; where vccd's blocking jobs
   (spawned programs) run.
6. **Formatting and lint.**  No mass reformat (it breaks blame and every open
   branch); `clang-tidy` for a short list of checks; our own clone scan.
7. **Rename windows.**  When each module is renamed relative to the open
   branches, and whether old names stay as aliases for a release.

## Status

### 2026-11-13: method written, survey done

Three read-only surveys (threading, duplication, passes and naming) over
main at 2e6d3e73; the counts in the last column of the table were rechecked
by grep, the rest are unaudited leads.  Nothing built, no code changed.
Next: P0 once PR #134 is on main.
