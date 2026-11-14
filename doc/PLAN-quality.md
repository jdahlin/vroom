# Code quality: how the plan gets written

**Goal:** before the C++ front end starts, make the toolchain's source sharp:
no duplicated code, shared patterns in one place, one scheduler that scales
with the machine, one pass per file with no repeated tree walks, and names
(files, functions, variables) that say what things do.

**This document is the method, not the plan.**  It says how the plan is
produced (stages P0-P4), what evidence it must rest on, and which decisions
are needed first.  P3 turns this file into the plan proper: workstreams,
steps, verification and status per step, as the other `PLAN-*.md` files.

Paths below are those of the reorganised tree (`src/...`): PR #134
(`PLAN-tree.md`) is on main and `retarget-paths.py text` rewrote the paths
here and in the reviews.  Line numbers are those of the tree the reviews read
(main at 2e6d3e73).

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
| Code generators | Three AST walkers that generate code: arm64's own (`arch/a64/codegen/`, 14 k lines), the shared `codegen/walk/` (x86-64 and VIR), wasm's (`arch/wasm/codegen/w*.c`).  Switch lowering, compound assignment, atomics, struct copy, constant folding, bit-field pieces and inline-asm template expansion exist two or three times, often the same code with other call names (`bf_piece` = `wk_bf_piece`) | division/switch recipe files: 15 files for 4 recipes |
| Linkers | `ld.c` (6.7 k lines), `wasmld.c`, `ldmacho.c`, `objcopy.c`: three symbol tables (`gfind`), two `map_file`, four `!<arch>` readers, private byte and output-buffer helpers, argv by hand in each | |
| Small helpers | FNV-1a written inline; `now()`; LEB128 encoders (6); `die`/`err`/`xmalloc` variants; `write_file` (3); private vector macros (7) while `include/common/vec.h` has one user; `include/port/mapfile.h` has one user | FNV in 17 files, `now()` 6, `realloc(` 111, `mmap(` 29 |
| Threads | Six private mechanisms, no shared pool: vccd workers (2 x all cores), emit workers per TU (`VCC_THREADS`, default 1), vlink `pfor` (P cores on macOS, lazily started, never joined), lexer I/O (3, the only ones with a QoS class), `vpp --batch` (hard-coded 6), lextest.  Three mutex+condvar queues, two atomic-index parallel-fors.  Pools nest without a common cap (vccd x emit, vccd + vlink).  `PLAN-fas2.md:160-173` describes one pool with work stealing; the code is not that | 6 `pthread_create` sites; the counts and sources as stated |
| Passes | One pass per file and a named pass table, for VIR first.  The direct path's passes are counted on VIR output and then deleted or rewritten as VIR passes, not tidied |
| Files with many jobs | `ld/ld.c`, `as/as.c` (4.1 k), `codegen/vir/build.c` (4.0 k: SSA construction, lowering interface, GVN, dominators, if-conversion, emission, vectorizer by `#include`), `server/vccd.c`, `driver/ppdriver.c` (`ppd_run1`: 1066 lines), `arch/a64/codegen/opt.c`.  18 functions over 300 lines | |
| Names | File names that hide their job (`hcentry.c`, `replay.c` with three meanings across modules, `exprop.c`, `tu.c`, `ppdriver.c`, `ccsnap.c`, `rsh.c`, `w*.c`); most arm64 emitter functions unprefixed (`mem`, `slot`, `treg`); state named `E->mf`, `E->K`, `W.la`, `VLT.chs`; `W` names three structs | |
| Documents | No document traces the pipeline end to end.  Stale headers: `include/codegen/emit.h`, `include/obj/obj.h`, `include/as/as.h` say arm64 only; AGENTS.md says `vir/` is linked into no tool.  No duplicate detection or lint in `bench/` or `ci/` | 58 distinct `getenv` knobs |

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
| L7 | Module boundaries | Who may include whom; what is public; dependency direction checked by a script (what C++ needs from `include/frontend/c/ccint.h` belongs here) |
| L8 | Global state | Single-instance globals that force a lock (`ldlock`, Mach-O link, objcopy), 80 thread-locals without teardown, 58 environment knobs: which are product options, which debug, which dead |
| L9 | Errors and limits | One policy for fatal errors, out-of-memory and untrusted input, instead of one `die`/`err` per file |
| L10 | Dead weight | Rejected experiments, fallbacks nothing uses, stale comments and plan text |

## Stages

| Stage | Output | How |
|---|---|---|
| **P0** Baseline | `bench/quality.py` and its numbers in Status below | A report script: token-level clone scan on vcc's own lexer (normalised identifiers, windows of N tokens, so renamed copies show), file and function sizes, counts of the private helpers above, thread mechanisms, environment knobs, files without a header comment, includes across modules.  Under 10 s, checkout only, so it can become a premerge ratchet |
| **P1** Reviews | `doc/review/quality-AREA.md`, one per area | One read-only session per area, in parallel, each reading its files in full and filling the template below.  Areas: (1) lex, pp, header cache; (2) diagnostics; (3) C front end; (4) shared codegen: `tu`, prepass, `walk`, `rec`, `opt`, `flow`; (5) the three walkers as one concept matrix: arm64, x86-64, wasm; (6) record optimizers and printers per target; (7) VIR: `vir/`, the builder, both lowerings; (8) assembler, `obj`, object writers; (9) linkers and binutils; (10) driver, server, shim, `rsh`, `ar`; (11) `lib/`.  Cross-cutting: (A) scheduling, with a measurement of idle and oversubscribed time in a kernel build; (B) traversal cost per pass in instructions (`vperf`), so fusing is ranked by what it saves |
| **P2** Target | `doc/ARCHITECTURE.md`, `doc/STYLE.md` | ARCHITECTURE: the pipeline as it is (missing today, and cheap), then as it should be: pass list with inputs/outputs, the analysis each pass may reuse, the scheduler, the shared library, the module dependency graph.  STYLE: file-name rules, prefix per module, header-comment template, glossary.  The decisions below are taken here, by measurement where they depend on speed |
| **P3** Plan | this file rewritten | Findings grouped into workstreams; each step has what, files, verification, the number it moves, and what it depends on.  Order: shared library first (everything else uses it), then splits (moves only), then renames in one window per module, then the unifications (walkers, linkers, scheduler, pass fusion), each behind the gates |
| **P4** Proof | one pilot step per workstream, done | Calibrates effort and shows the gates catch mistakes (for example: FNV to `include/common/hash.h`; one pass out of `opt.c`; one module renamed by script; `vpp --batch` on the new scheduler).  A fresh reviewer then attacks the order; after that execution starts |

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

## Decisions (Johan, 2026-11-13)

Taken in a question round after P1.  They replace "Decisions needed" above
where the two differ.

| Topic | Decision |
|---|---|
| Main goal | Readable by a newcomer: one job per file, names that say the job, a comment on every module.  Duplication, speed and C++ readiness follow from the order below, they do not lead it |
| Timebox | None fixed; Johan says when the tree is ready for C++ |
| Requirement | One binary compiles for every target, as clang does (cross compilation).  The target is data chosen at run time; host code never decides target behaviour |
| Output rule | Byte-identical is the proof as long as nothing better exists, not a goal for ever.  A step that changes output is named as such and validated another way |
| Method | Module by module, each finished in one go: split, rename, comment |
| Rename depth | Files, a module prefix on every file-scope function, long-lived state (struct fields, globals).  Short locals stay |
| Open branches | Ignored: no aliases, no windows |
| Pilot | The shared library (`lib/` to `common/`) |
| Order after it | Bottom-up by layer, with VIR moved up: lex and pp, diagnostics, C front end, VIR and its builder, what survives of the code generators (ABI, op tables, printers, encoders), assembler and object, linker, driver and server.  Modules that do not include each other run in parallel once `common/` has landed |
| Code generation | **VIR for everything** (2026-11-13, after a full kernel build with `VCC_VIR=1` at 81.2 s against 79.1 s; 2-3 % loss accepted).  The direct emitters are scheduled for deletion and get no quality pass; optimizations outside VIR move into it.  Gates, pass-by-pass fate and order: `doc/quality/VIR-MIGRATION.md`.  This replaces the earlier decision (safe moves, then a pilot of arm64 on the shared walker) |
| Passes | One pass per file and a named pass table (time, disable, verify by name), identical output.  Fusing walks comes later, from the per-pass timings |
| Linker | One linker core with output formats, not three linkers.  (Not the recommendation; it fits the one-binary requirement) |
| Scheduler | Now only the measured waste: cache flush between process groups, vlink waking every thread for a small link, batch worker stacks.  One scheduler is its own plan after the readability pass |
| wasm | Disabled now, build and CI, and left to rot until needed (its own PR) |
| Error policy | Fail the job, never the server: bad input, limits and out-of-memory end one job with a diagnostic; `abort` only for internal inconsistency, through one macro; standalone tools print and exit |
| Environment switches | Product knobs become options that travel with the job; test aids go in one registry read from the job's environment; finished experiments are deleted |
| Ratchet | `bench/quality.py --check` gates a module once it has had its pass; other modules are reported only |
| Bugs from the reviews | Fixed now, each with a test, in their own PR |
| Done, for a module | One job per file (exceptions allowed where they make sense, with the reason in the file's comment); every name follows STYLE.md; an opening comment on every file.  Hitting a number is not required |
| Diagnostics | Its own module below pp, behind a small source-map interface; assembler, linker and driver report through it and keep their lld and llvm-mc wording |
| Options | One option table per tool, read by the parser, the capability list and `--help`; an unknown option is an error, as clang's.  This changes behaviour for options ignored today |
| Target tables | One op table per CPU (operands read and written, flags, branch kind, text, encoding) that passes, printer and encoder read; done in each back end's pass |
| Names | `module_noun_verb` in full words (`pp_macro_expand`, `ld_archive_add_member`); abbreviations only from a short list in STYLE.md |
| Who does it | The top model writes a module's work order (file map, rename table, comment drafts) from its review; a cheaper model and scripts carry it out; the top model checks the result |
| Delivery | Johan reads results, not previews.  Each pass must land safely on tooling alone (identical output, gates, premerge) and modules must be workable in parallel: a work order names the files it owns, and two orders never share a file |
| Shared pool (later, same day, after wave 1) | vccd's shared pool stays off by default and uncapped; vccd joins make's jobserver later, after the cleanup.  No scheduler work in wave 2 |
| Where VIR's optimizer lives (later, same day) | On VIR itself: new passes, and passes moved from the direct emitters, work on the documented VIR format and are testable with `virtest` on `.vir` files.  Existing pre-IR passes stay until each is ported.  A 2-3 % compile-time loss on VIR is acceptable.  Moving existing passes onto VIR is parked: see the next row |
| Moving optimizations onto VIR (later, same day, 20:58) | Parked for later.  It is fine that some optimization stays on the AST and in the record optimizer and some is on VIR.  The row above still holds for where a new VIR pass goes; what is parked is the moves |
| Ratchet regressions of wave 1 (later, same day, 21:45) | Accepted: `bench/quality-baseline.json` is blessed at main's numbers (redundant_tokens 45173, realloc_calls 97, thread_locals 112, environment_knobs 65, functions_over_300_lines 31).  A focused run to bring them back down comes later; PR #196 lists which PR moved each number.  Track G (linker) is no longer held: an agent finishes `WORK-linker.md` from PR #183 on |
| Struct field renames (later, same day) | Done with a mode of vcc's own front end that reports every use of a given struct field with its location; the rename script applies them.  No clang tooling |
| Integrator (later, same day) | Coordinates as before: spawns workers, checks proofs, merges; the PR's agent keeps it mergeable and green |

Open, each waiting for a measurement or a closer look:

- `frontend/c/replay.c`: keep, clean, or move to a branch.  Measure its saving on
  today's tree first.
- When `struct cc` is split into AST and parser.  Check the 24 fields code
  generation reads against the hot paths; the split must cost nothing.
- (Closed by the VIR decision: a target is a VIR lowering and its op
  table; the walker has one instance.)

## Status

### 2026-11-13: wave 1 merged, wave 2 started

Wave 1 merged PRs #134-#175 (41 PRs): the baseline script, 18 reviews and
the work orders, file splits for common, as, ld, pp, driver/server, diag,
cc and the VIR builder, renames for common and as only (pp and diag have
rename tables), the shared scheduler's stages 1-3 behind `--shared-pool`
(off), the first VIR steps (analyses computed once, lowerings under
`arch/<cpu>/vir`) and the builder's named pass table.

Wave 2's tracks:
- pp and diag renames; rename tables, then renames, for ld, driver/server,
  cc, vir and codegen.
- The field-use mode of the front end and the assembler's remaining field
  renames.
- Opening comments; pilot steps 4-6 of WORK-common; the link context.
- The first optimizer pass on VIR itself, then VIR-MIGRATION steps 2e and 3.
- Diagnostics as a module and the option tables, after the renames.

#### Wave 2, decided by the owner during the wave

- Track G (the linker's context struct and the one linker core): released
  later in the wave.  The link context is done in four PRs (#183 design and
  step 1, #220 output side, #222 input side, then the context as a parameter):
  no per-link state is left at file scope, `ld_main` creates one context per
  link.  Then the field renames of `rename-ld.tsv` (#225) and the part files
  compiling alone (#230); WORK-linker step 3, the shared layer, is under way
  (#231: file mappings through `port/file.h`; one archive reader in
  `common/archive-reader.c` for vlink, vnm and ar), then step 4, one linker
  core (`doc/quality/WORK-linker.md`).
- Track I (diagnostics as their own module; one option table per tool) is held
  by owner, wave 2, and not started.
- The field-use mode (track D) stays a plain rename helper; its output is not
  generalised for other tools.
- New: a cheap lint mode `vpp --lint` (STYLE.md naming for declarations, front
  end only, normal diagnostics), not wired into premerge.

Merged in wave 2 so far (#176 onwards):
- #176 wave 2 decisions and status.
- #177, #178, #179, #180: opening comments for `src/as`, `src/obj` and
  `src/platform`, `src/arch/*/as` and `arch/a64/obj`, `src/utils` and `src/bin`.
- #181, #182, #186, #188, #190: rename tables for driver/server/utils/bin, the
  linker, the C front end, VIR and the surviving code generator.
- #184: design of the struct-field rename mode.
- #185: VIR optimizer design (passes on VIR itself; WORK-vir decision closed).
- #187, #191, #192: WORK-common's pilot, steps 4-6 (variants merged, the
  missing facilities, comments and the gates).  The pilot is done.  Left open:
  `common/` still includes `port/` in four places (`intern.c`, `intern.h`,
  `text-buffer.h`; the gate holds the count at 4); `qbig` in `softfloat.h` is
  not moved onto `bigint.h`; `unlikely_vi` is still in `intern.h` (a bare
  `unlikely` collides with private defines).
- #189: lex, pp and diag renamed by table (fields with short names deferred).
- #194, #195, #197, #199, #200: opening comments for `src/driver`, `src/server`,
  `src/codegen` (shared part), `src/include` and `src/vir`.
- #196: the quality baseline blessed for the legitimate moves of waves 1 and 2.
- #198: the linker's rename table applied (scoped), file moves, WORK-linker status.
- #201: `vpp --lint`, the naming check against STYLE.md module prefixes.
- #202: `VCC_FIELD_USES`, the front-end mode reporting uses of selected struct fields.
- #203: VIR pass infrastructure (WORK-vir 4.0): `src/include/vir/pass.h`, registry,
  `vir_function_compact`, `virtest --passes`; output identical.
- #204: one lowering driver for arm64 and x86-64 (step 2e, Q-VIRB-02).
- #205: `field-uses.py` and `rename-table.py --fields`: struct field renames from
  the front end's report.
- #206: the remaining field rows of `rename-as.tsv`, the first user of `--fields`.

#### Track H (VIR optimizer) stopped, owner's decision 2026-11-13 20:58

Track H stopped after what merged: #203 and #204.  Step 4.1
`redundant-extensions` is parked: draft PR #207, branch
`quality/vir-redundant-extensions`, not to be merged.  Measured for the
record, with the same vpp and the pass on and off, the record clean-up `exts`
still on: zlib 0.00 %, SQLite -0.095 % instructions executed, compile cost
+0.13 % cc.  The pass's worker measured the `exts` counter on VIR output at
-28 % (suites and SQLite) and the pass's own cost at 0.3 % of build+lower on
the suites but 3.0 % on SQLite.  Whether `exts` can be removed was not
measured.  Step 3 (inline assembly, debug info on VIR) is not pushed in this
wave: 3.0 has a local unpushed WIP branch `quality/vir-debug-rows`, blocked on
`finish_function` in `src/arch/a64/codegen/func.c` dropping shrink-wrapping
under `-g` (so `-g` would change `.text` with VIR).  No work on deleting the
record optimizer's clean-ups.

### 2026-11-13: on the reorganised tree

PR #134 is on main (`src/`, `doc/`); PR #136 took wasm out of the default
build.  This plan, the reviews and the work orders moved to `doc/` with
them.  `bench/quality-baseline.json` is blessed again on `src/`: the
numbers that moved are path-dependent (includes of another module's
internal header 4 to 7 and mutual module pairs 4 to 5, because public
headers now sit under `include/`), one more environment switch from main,
and 2 redundant tokens.

Rule for the parallel work: the agent that opens a PR keeps it mergeable and
green until it is merged; the integrator coordinates, reviews scope and
merges, and does not fix other agents' PRs.

### 2026-11-13: P1 done, 18 reviews

`doc/review/quality-*.md`: 18 reviews, 5848 lines, 373 findings, each
reviewer having read its files in full (the "Not covered" section of each
says what it did not).  `doc/review/README.md` has the areas, the rules and
the template.  Evidence checked afterwards by grep or by running in six of
them (as-obj, backend-records, vir-build, diag, linkers, cc-cache); the rest
is as the reviewers wrote it.

What the reviews settle or change:

- **arm64 cannot go onto the shared walker without changing its output**
  (walker-a64: ten gaps in `LO()`, a 36-concept matrix: a quarter the same
  code renamed, a third the same algorithm written differently, a quarter
  really different; the two walkers already disagree in 10 places).  First
  step instead: about 800 lines of target-independent code sit in
  `arch/a64/codegen` and `emitimpl.h` is arm64's header (codegen-shared,
  boundaries); they move with identical output.  Then a pilot.
- **wasm stays its own walker for control flow and calls**; the C-level
  decisions (19 of 32 concepts) are shared.  `vcc-same.py` has no wasm mode,
  so nothing there can be proved identical yet (walker-wasm).
- **One record type**: arm64's `ins` and the shared `rec` are the same 16
  bytes under other field names (backend-records).
- **`struct cc` is the obstacle for C++**: parser state, AST tables and
  scratch in one struct that codegen reads 24 fields of; the split into
  `ast.h` and the parser's part is drafted (cc-expr, cc-decl, boundaries).
  The cc-emit cycle is one call: `cc_run` calls `emit_tu`.
- **`common/` first**: 11 facilities ordered by copies replaced; vectors
  about 330 call sites, 11 hand-written string tables, FNV in 18 files, and
  no out-of-memory policy (six behaviours) (lib, diag).
- **Passes, measured** (instructions, warm, one thread): kernel TUs are pp
  53 %, parse and sema 33 %, emit 13 %; SQLite is emit 58 %.  After VIR's
  allocator the arm64 clean-ups run again: 10.5 % of a VIR compile.  No pass
  list exists as data anywhere; VIR computes dominators 3 times and the CFG 5
  times per function (passes, vir-ir, vir-build).
- **Scheduling, measured** (one run each, loaded machine): two builds
  through one vccd flush the caches at every switch (168 TUs: 11.1 s against
  0.8 s); compiles are off the CPU 43-72 % of their time in old build logs;
  a small link is 2.3x slower with vlink's pool than without; emit threads
  leak their scratch (scheduling).

Bugs found on the way (not quality work; each needs a test and a fix):

| Where | What | Status |
|---|---|---|
| `driver/ccsnap.c:344-365` | a syntax error after a restored header snapshot kills `vpp --batch -j 4` (and the code runs in vccd) | reproduced here: exit 139 in 4 of 4 runs, exit 1 with `-j 1` or `VCC_NODC=1`; the cause (a kept pointer into another thread's stack) is from reading |
| `ld/ld.c:65,1977` | `x86_ld` is never cleared, so vccd links arm64 as x86-64 after one x86-64 link | no reset in the source (grep); not run |
| `codegen/vir/a64.c:1270` against `arch/x64/codegen/vir.c:91` | the dense-switch case count differs when a range spans all 64 bits | read; not run |
| `server/vccd.c:405,426` | `argc + envc` summed in 32 bits: a malformed request reads `argv[-2]` | reported; not checked |
| `pp`: `#embed` | read through the token cache: a BOM or `1'0` embeds wrong bytes | reported as confirmed by its reviewer; not checked here |
| `ld/ld.c` `rel_width`, `wasmld.c` | corrupt x86-64 object can write past a section; unchecked counts | from reading |
| `frontend/c/func.c:299` | wrong `-Wreturn-type` on an enum-covered switch (TODO.md:1660) | reported as verified against clang |
| `bin/vpp/vpp.c:91` | batch workers have a 512 KB stack: a test file ends a batch with SIGBUS, and nesting limits differ 30x from `vpp` | reported by two reviewers |

Next: P2.  The decisions of all 18 reviews (about 120) collected into one
list, grouped, with a recommendation each, before `ARCHITECTURE.md` and
`STYLE.md` are written.  Cost note: the reviews ran on the session's model
and took about 6.7 M tokens; P2 and P3 read the review files and need no
such fan-out.

### 2026-11-13: P0 done, the baseline

`bench/quality.py` reads the tree once (no build, 3 s) and prints the report;
`--clones N` lists the longest duplicates, `--json FILE` has every list
(clones with both line ranges, repeated static names, knobs, module edges),
`--check` fails when a ratchet number is above `bench/quality-baseline.json`.
It runs unchanged on the reorganised tree (checked on PR #134's `src/`: same
numbers, other paths).

Main at 2e6d3e73, `src` without tests and the 18 generated files:
270 files, 140.0 k lines, 3750 functions, 1.28 M tokens.

| Lens | Number | Baseline |
|---|---|---|
| L1 | redundant tokens, runs of 60+ (the later copy only) | 44 645 = 3.48 %, 676 runs; 92 exact, 217 repeat another file |
| L1 | the same with runs of 30+ / 100+ tokens | 13.0 % / 0.99 % |
| L2 | files with an FNV hash of their own | 18 |
| L2 | `realloc` / `mmap` call sites outside `lib/` | 95 / 25 |
| L2 | LEB128 functions | 15 |
| L2 | static function names defined in several files | 79 names, 102 extra definitions (`now` x6, `worker` x4, `err` x4) |
| L3 | `pthread_create` sites / CPU-count probes | 6 / 3 |
| L3 | mutex and condition variable uses | 31 |
| L4 | switches on a node kind (approximate) | 142 |
| L4 | files of 2000+ lines / functions of 300+ and 150+ lines | 14 / 30 and 121 |
| L6 | files without an opening comment / with a one- or two-line label | 11 / 141 of 270 |
| L7 | includes across modules / of another module's internal header | 116 / 4 |
| L7 | module pairs that include each other | 4: `cc`-`emit`, `cc`-`driver`, `driver`-`vccd`, `arch/a64`-`obj` |
| L8 | thread-locals / file-scope mutable statics (approximate) | 103 / 275 |
| L8 | environment knobs | 58 names, 77 `getenv` calls |

Where the duplicates are, in tokens: inside `cc` 8.5 k, `codegen/walk` 4.2 k,
`arch/x64/as` 3.9 k, `arch/a64/codegen` 3.9 k, `arch/a64/as` 3.7 k, `codegen/vir`
2.9 k; between `arch/a64/codegen` and `codegen/walk` 2.4 k.  By share of a
directory: `arch/x64/obj` 52 % (its `fixup.c` repeats arm64's), `arch/x64/ld`
10 %, `codegen/walk` 8.8 %, `platform/wasm` 8.4 %.  The longest: the inline-asm
template loop (x86-64 = arm64, 265 tokens), `include/common/hash.h` = `as/asimpl.h`
(250), the call sequence in `walk/stmt.c` = `a64/emit/call.c` (226),
`virtest.c` = `viralloc.c` (214).

How a duplicate is decided: a run matches another place token for token with
identifiers and string literals ignored; it uses at least 10 different
tokens; and unless it is an exact copy, 85 % of its identifiers map one to
one and 30 % of them are names used twice (this removes enumerator lists and
initializer tables, which map one to one whatever they hold).  Checked by
reading 16 sampled runs under 90 tokens: before the last rule 2 of 7 were
tables; after it 9 of 9 were code written twice.

What the numbers say about the survey: its leads hold.  Two corrections:
30 functions are over 300 lines, not 18; and textual duplication is 3.5 %,
so most of the three walkers' overlap is the same algorithm written
differently, which a clone scan cannot see.  That part needs the concept
matrix of review area 5.

Not measured: identifier quality (needs STYLE.md's rules first), walks per
function and their cost (review B, in instructions), idle and oversubscribed
threads (review A).  Not done: the ratchet is not in `ci/premerge.sh`; with
130 open branches a failing premerge for one more `realloc` is a decision for
P3.  Bless the baseline again when PR #134 lands (the include numbers move
with the paths).


### 2026-11-13: method written, survey done

Three read-only surveys (threading, duplication, passes and naming) over
main at 2e6d3e73; the counts in the last column of the table were rechecked
by grep, the rest are unaudited leads.  Nothing built, no code changed.
