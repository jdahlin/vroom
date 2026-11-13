# Quality review: diagnostics engine and error reporting (L9) (2026-11-13, main at 2e6d3e73)

## 1. Summary

The engine itself is in good shape as a design: every diagnostic is a record
(`vdiag`), three text styles and three machine formats render from it, and
nothing runs without an error.  Its problems are of placement and repetition:

1. **Only the C front end can use it.**  It is four files inside the
   preprocessor's unity TU and reads the `pp` struct directly.  The emitter
   reaches it through `pp_vdiag`; the driver, the assemblers, the object
   writers, the linkers, `ar`, `rsh` and VIR each have a private reporter (14
   helpers, section 4 Q-diag-07).  None of their messages are records, so they
   are missing from JSON/SARIF, from vccd's store and from "N errors generated".
2. **There is no policy for running out of something.**  A failed allocation
   is handled in six different ways, three of which end the vccd server; four
   input-driven limits still `abort()` with the message on the server's
   stderr although `vjob_fail` was written for exactly that (Q-diag-01, -03).
3. **The engine's own files hold copies.**  The clang-style and vcc-style
   renderers share five pieces by copy, `cc/diag.c` has six copies of "report a
   warning of a flag" that have drifted apart, and `pp/diag.c` (1875 lines) is
   eight jobs under a header that names one (Q-diag-08, -09, -10).

## 2. Inventory

| File | Lines | What it actually does | Better name | Header |
|---|---|---|---|---|
| `pp/diag.c` | 1875 | (a) source map: location to file entry, line, presumed name (9-110), used by `__LINE__` and line markers too; (b) `#pragma diagnostic` states and the `-W` command-line state (192-455); (c) source-text queries for the parser: `pp_src`, `pp_tok_end`, `pp_spells`, `pp_include_point` (144-190, 457-564); (d) display width (566-602); (e) clang's snippet renderer (604-841, 1636-1745); (f) the macro backtrace rebuilt from the definitions (843-1315); (g) the record builder (1317-1570); (h) emit: queueing, dedup, counting, fatal exit (1747-1875); (i) `VCC_TRACK_DIAG` through `atos` (1602-1634) | split: `srcmap.c`, `diagflags.c`, `diagrecord.c`, `diagmacro.c`, `diagclang.c` | label only, and stale ("diagnostics in clang's text format") |
| `pp/diagtext.c` | 648 | the vcc text style (marks, labels, folded notes), the narrated style, the type diff | `diagvcc.c` (next to `diagclang.c`) | good |
| `pp/diagout.c` | 847 | JSON lines, SARIF, the HTML page, vccd's record sink, and fix-it application: the unified diff and `--fix` (621-847) | keep for JSON/SARIF/HTML; fix-its to `diagfix.c` | good, but does not mention the fix-it half |
| `cc/diag.c` | 576 | the front end's reporting entry points (1-135, 283-568), parser recovery: `rec_save`, `rec_restore`, `skip_*` (137-281), `punct_spell`, `idname` | recovery to `cc/recover.c`; `idname`/`idstr_` to `token.c` | label only ("diagnostics in clang's format") |
| `pp/pp.h` (136-314) | 180 | the public API: about 50 functions, a third of them source queries, not diagnostics | a `diag.h` of its own | good per function, no overview of the two record APIs |
| `pp/ppimpl.h` (468-500, 570-572, 677-716) | 80 | engine state inside `struct pp`, the `error`/`fatal`/`warn` macros, cross-file prototypes | | the "diag.c" block (678-689) lists functions of `setup.c`/`text.c` |

**What the module is for (draft opening comment).**  A diagnostic is a record
(`vdiag`: level, id, flag, message, primary location, labelled spans, fix-its,
help, notes as children) built in the TU's arena on the error path only.
`pp_diag_emit` appends it to the TU's list, moves a location inside a macro
expansion to the invocation and adds the "expanded from macro" notes, then
renders: the clang and narrated styles at once, the vcc style and JSON when
the next top-level record starts (a record's notes arrive after it), SARIF and
HTML at `pp_diag_finish`.  Invariants a newcomer must know: a location is a
`uint32_t` in the TU's file-entry table, `UINT32_MAX` means none; a note
emitted after a diagnostic becomes its child; the first line of every style is
clang's and `bench/diag-render.py` rebuilds the vcc text from JSON byte for
byte, so a renderer change is made in both; one thread per `pp` (the emitter
takes its own lock, Q-diag-18); a fatal record ends the TU by `longjmp`.

## 3. Passes and data flow (L4)

Nothing here walks the AST.  The repeated work is in the lookups:

- **`-W` state.**  `wstate` (`pp/diag.c:313-360`) walks all `-W` arguments
  twice with `strcmp` for every query; `pp_diag_wlevel` calls it per reached
  warning site.  Three caches were added around it instead of one table:
  `pp->wc[]` (`pp/ppimpl.h:437,476-477,584-589`), `cc`'s `ext_off` and
  `next_k/next_v` (`cc/diag.c:473-519`), and `nenable` by pointer
  (`pp/diag.c:307-310`).  Not measured.
- **Finding a macro's `#define`.**  Three implementations, two of which scan
  every token of every file of the TU per note: `pp/expand.c:18-41`
  (`macro_note_`), `pp/diag.c:916-969` (`expansion_note_scan`), and the sorted
  index `mdx_update`/`mdef_body` (`pp/diag.c:990-1059`) that makes the other
  two unnecessary.
- **Location to file entry.**  `loc_fent` (`pp/diag.c:9-24`) is a binary search
  run 2-6 times per rendered record (`loc_ok`, `diag_where`, `pp_where`,
  `loc_line` each search again); `pp/setup.c:808-818` has the same search
  written out again.
- **Line arithmetic.**  `line_of`/`line_start` (`pp/diagout.c:698-714`) count
  newlines from the file's start inside the per-edit loop of `fix_patch`
  (754-776): quadratic in file size per hunk; error path only.
- **The include chain** is walked in `pp/diag.c:621-626`, `pp/diagout.c:97-98`
  and twice in `pp/diagout.c:300-309`.

Data flow for the other tools (no passes, only how a message travels):

| Module | User error | Out of memory | Internal inconsistency | May end the process |
|---|---|---|---|---|
| `pp`, `cc` | record with location (about: `cc_error` 237 sites, `fatal(c` 45, pp macros 48) | arena full: `abort` (`lib/arena.h:260-262`); shared region: `vjob_escape` (`pp/file.c:25`) | `delayed_bug` record (`cc/diag.c:116-135`); `abort` (`cc/diag.c:510`, `cc/builtin.c:354`) | `exit(1)` at `pp/diag.c:1823` when no jump target is set (dead today: the one `pp_open` caller sets one, `driver/ppdriver.c:1771-1778`) |
| `emit`, `arch/*/emit` | record through `pp_vdiag` (`efatal` 173 sites) | `abort`, silent (`emit/rec/slots.c:13`, `arch/a64/emit/opt.c:236,242,459,869,1746`) | `abort`; VIR: `vw_fail`/`vl_fail` jump back to the direct emitter (`emit/vir/build.c:65`, `emit/vir/a64.c:78`) | yes: "function too large" (`arch/a64/emit/a64impl.h:37-39`, `func.c:357-359,409-411`), "emit log full" (`emit/emitimpl.h:1040-1042`), `lib/tbuf.h:41-43` |
| `as`, `arch/a64/as`, `arch/x64/as` | `err(a, ...)` (187 sites) into `a->msg`, "file:line: error:", at most 20 (`as/as.c:28-42,2592-2601`); the caller prints | regions: `vjob_fail` (`as/asimpl.h:140`) | `err` | no (`as/vasm.c:23` is the tool) |
| `arch/wasm/as` | `was_error` (59 sites), first error only, "line N:" (`as.c:18-33`) | `abort`, silent (`as.c:41,97,405,564`) | — | yes, on OOM |
| `obj`, `arch/*/obj`, `platform/linux/elf.c`, `platform/darwin/macho.c` | `elf_error` (43 sites), first only, no location (`obj/obj.c:27-34`) | region full: `vjob_fail` (5 sites) | — | only without a job (`obj/obj.c:24`) |
| `platform/wasm/wasm.c` | — | `abort`, silent (281-286); six more `calloc` not audited | — | yes |
| `ld/ld.c`, `arch/*/ld` | `error`/`warn` (75 sites) to the request's stream, limit 20 (`ld.c:89-134`) | `bigalloc`: process stderr + `abort` (187-195); `rsp_alloc`: stderr + `exit(1)` (6573-6578) | `error` | yes; also `exit(0)` for `--version` (3828-3832) |
| `platform/darwin/ldmacho.c` | `error` (38 sites) to `errf`, limit 20 (69-79) | `errf` + `exit(1)` (81-91, 676-678) | — | yes: in vccd for host programs |
| `ld/objcopy.c` | `oerror` (38 sites) to `oerr` (49-60) | stderr + `abort` (62-70) | — | yes |
| `ld/wasmld.c` | `err` (33 sites), always the process's stderr (43-55) | `abort`, silent (33-41, 85-87); `malloc` unchecked (67) | — | tool only (called from `main`, `ld.c:6689-6700`) |
| `vccd/ar.c`, `vccd/rsh.c` | `err` to the request's fd (`ar.c:80-88`, `rsh.c:469-476`) | a flag, the request fails (`lib/buf.h` `vsbuf`, `vzbuf`) | — | no: the only modules written for a server |
| `vccd/fixdep.c`, `vccd/dstore.c` | `dprintf(errfd)` | `abort` (`fixdep.c:61`, `dstore.c:59,117`) | — | yes |
| `vccd/vccd.c` | `say()` to the client (441-448) | reply "out of memory" (1176) | — | start-up and drain only (1795, 1858) |
| `driver/ppdriver.c` | 31 `dprintf(errfd, ...)` (about 27 are errors), no location, three prefixes | `abort` (80, 538); `ccsnap.c:233-235` message + `abort` | — | yes, on OOM |
| `vir/` | parser: `perr_at` fills `{line, col, msg}` and jumps (`parse.c:174-185`); verifier: `vfail` list; allocator: `fail` into `o->err` (`alloc.c:367`) | `abort`, silent: `vir.c` 7 sites, `parse.c` 13, `allocchk.c` 2 | `abort` (`arch/x64/emit/vir.c:211,951`) | yes, on OOM |
| `lib/` | — | `mmap`: `perror` + `abort` (`arena.h:64-66,176-178,227-229`); `vbuf`: `abort` (`buf.h:47`) | `abort` (`intern.c:286,314`) | yes |

Call-site counts in these modules (`as ld platform vccd driver vir lib obj
arch/*/{as,ld,obj}`, comments left out): `abort()` 51 (89 in the whole tree),
`exit(` 8, `_exit(` 2, `fprintf(stderr` 51, `dprintf(` 39, `perror(` 16,
`assert(` 0.  The tree has no assertion macro at
all: an internal inconsistency is an unexplained `abort()` or goes unnoticed.

## 4. Findings

| Id | Lens | Class | Evidence | What it costs | Proposed home or fix | Size | Risk | Proof |
|---|---|---|---|---|---|---|---|---|
| Q-diag-01 | L9 | error policy | "function too large" + `abort`: `arch/a64/emit/a64impl.h:37-39`, `arch/a64/emit/func.c:357-359,409-411`; "emit log full": `emit/emitimpl.h:1040-1042`; "emitter buffer overflow": `lib/tbuf.h:41-43`; "preprocessor arena full": `lib/arena.h:260-262`; `vregion_push`: `lib/arena.h:242-244`; `pp/dircache.c:68-70` (silent) | a limit one input reaches ends vccd and every job in it; the text goes to the server's stderr, the client sees a dropped connection | all through the one job-failure call of Q-diag-02 | M | low | new `vcc-stress.py --vccd` cases (none exist for these limits: missing) |
| Q-diag-02 | L9 | near-duplicate | `vjob_fail` + `vjob_fail_fd` + `vjob_jb` (`obj/obj.c:15-25`, `obj/obj.h:145-151`) and `vjob_escape` + `vjob_escape_fd` + `vintern_full_hook` (`lib/intern.c:137-152`, hook `driver/ppdriver.c:633-637`); both fds set together at `driver/ppdriver.c:1335,1959` | two "end this job" calls with two fds and a hook that exists only because `lib/` cannot see `obj/`'s jump buffer | `lib/job.h`: one thread-local job (jump target, error fd, tables-full flag), `vjob_fail(msg)` | S | low | `vcc-stress.py --vccd`, `vccd-test.py` |
| Q-diag-03 | L9 | error policy | six behaviours for a failed allocation, rows of the table in section 3; unchecked in the engine itself: `pp/diag.c:677`, `pp/diagout.c:637`, `pp/diagtext.c:178-180`, `cc/diag.c:378` | nobody can say what vccd does when memory runs out; `ldmacho.c`'s `exit(1)` looks like an ordinary failed link to make | decision 1; then `lib/xalloc.h` (`vxmalloc`, `vxcalloc`, `vxrealloc`, `VGROW`) | M | low | build both ways; `vlink-bad.py`; a fault-injection run is missing |
| Q-diag-04 | L7 | misplaced | the engine is in the pp unity TU and uses `struct pp` (counts below the table) | no other tool can report through it; C++ will be the second front end that must go through pp to report | decision 2: module `diag/` behind a source-map interface | L | medium | `vcc-diag.py`, `vcc-mutate.py --formats`, `vcc-driver.py`, `vcc-same.py`, `vbench.py gate` |
| Q-diag-05 | L9 | error policy | `driver/ppdriver.c`: about 27 errors printed with `dprintf(errfd`, prefixes "vcc: error:" (982, 1314, 1344, ...), "error:" (1072, 1384), "clang: error:" (1100); one to the process's stderr instead: `fprintf(stderr` at 1235 | option errors are not records (absent from JSON/SARIF and the summary); under vccd the `-ftls-model=` error at 1235 never reaches the user | location-less records with a tool origin (interface below) | M | low | `vcc-driver.py` (compares command-line errors with clang) |
| Q-diag-06 | L9 | error policy | assembler text is "file:line: error: msg" (`as/as.c:36`), the driver prints it after "vcc: error: " (`driver/ppdriver.c:1468-1469,1937-1938`): two level words on one line; `.warning` goes to the process's stderr (`as/as.c:2425-2427`): lost under vccd; inline-asm failures are records at location 0 (`emit/tu.c:275-282,335,664,673`) | a `.S` error cannot be parsed as `file:line:col: error:` by an editor; a warning disappears in the server log | records with a (file, line) location | M | medium (llvm-mc text parity) | `vas-obj.py`, `vas-corpus.py`, a diag case for a `.S` error (missing) |
| Q-diag-07 | L1 | near-duplicate | reporters: `ld/ld.c:89-134`, `platform/darwin/ldmacho.c:66-79`, `ld/objcopy.c:45-60`, `ld/wasmld.c:43-55`, `vccd/ar.c:80-88`, `vccd/rsh.c:469-476`, `vccd/vccd.c:441-448`, `obj/obj.c:27-34`, `arch/wasm/as/as.c:18-33`, `as/as.c:28-42`.  Allocators: `ldmacho.c:81-89` `xmalloc`, `objcopy.c:62-70` `xcalloc`, `vccd/dstore.c:54-64` `xrealloc`/`xstrdup`, `driver/ccsnap.c:229-238` `dalloc`, `ld.c:187-195` `bigalloc`, `ld.c:6573-6582`.  Grow macros: `ld/wasmld.c:33-41` = `arch/wasm/as/as.c:35-43` (exact), `ldmacho.c:90-91`, `vir/vir.c:770-775`, `vir/parse.c:20-25` | ten ways to print "TOOL: error: text" and count it, with three error limits of 20 written separately (`ld.c:86`, `ldmacho.c:73`, `as.c:2600`) | reporters: the engine (Q-diag-04); allocators: `lib/xalloc.h` | M | low | `vlink-opts.py`, `vlink-bad.py`, `vobjcopy-cmp.sh`, `var-cmp.py`, `vsh-test.py` |
| Q-diag-08 | L1 | near-duplicate | "report a warning of a flag" six times: `pp/diag.c:434-455` `pp_warnx`, `cc/diag.c:393-412` `cc_warnx`, `416-433` `cc_errorx`, `437-457` `cc_warn_begin`, `521-542` `ext_warn`, `546-568` `rt_defer` (scan: 74 tokens exact 530-540 = 445-455) | they disagree: see below | one `warn_begin(c, flag, on, groups, loc, fmt, ap)` returning the record or NULL; the six become wrappers | S | low | `vcc-diag.py`, `vcc-sema.py` warn, `vcc-mutate.py` |
| Q-diag-09 | L1 | duplicate | fix-it hint line: `pp/diag.c:812-835` = `pp/diagtext.c:275-295` (109 tokens exact); blank trimming: `pp/diag.c:689-697` = `708-715` = `pp/diagtext.c:314-322`; tab expansion: `pp/diag.c:661-673` = `pp/diagtext.c:162-175`; line start/end: `pp/diag.c:643-648`, `764-769` = `pp/diagtext.c:42-58`, again `pp/diagout.c:698-714`; colour codes: `pp/diag.c:121-133` = `pp/diagtext.c:25-30`; level names: `pp/diag.c:1356` = `pp/diagout.c:64`; span and fix-it JSON: `pp/diagout.c:130-140` = `140-150` | a change to clang's fix-it or tab rule is made in two renderers and in `bench/diag-render.py` | `diagsrc.c`: source-line helpers, the hint line, the trimmed range, one colour table | S | low | `vcc-diag.py` (text identical to clang in the clang style), `vcc-mutate.py --formats` |
| Q-diag-10 | L5 | mixed file | `pp/diag.c`: jobs (a)-(i) of the inventory; `cc/diag.c:137-281` is parser recovery; `pp/diagout.c:621-847` applies fix-its to files | the source map that `__LINE__` and line markers use (`pp/builtin.c:234,242`, `pp/setup.c:604-903`) is in a file named for diagnostics | the split in the inventory, moves only | M | low | unity and file-by-file builds, `vcc-same.py` |
| Q-diag-11 | L1 | duplicate | finding a `#define`: `pp/expand.c:18-41`, `pp/diag.c:916-969`, `pp/diag.c:990-1059`; `loc_fent`: `pp/diag.c:9-24` = `pp/setup.c:808-818` (73 tokens exact) | two full scans of the TU's tokens per note where an index exists | `mdef_body` for all three; `tok_col` calls `loc_fent` | S | low (`tok_col` is on the `-E` path: gate) | `vcc-diag.py`, `vcc-diff.py text`, `vbench.py gate` |
| Q-diag-12 | L2 | pattern | a warning is a string, its groups an array named at each call: `w_all`, `w_conv`, `w_gnu`, `w_ped` (`cc/diag.c:364-366,461-462`), `w_selfassign` identical to `w_all` (364 = 366); `ext_flags`/`ext_groups` kept in step by hand, an `abort` when they are not (`cc/diag.c:465-471,507-512`); three caches (section 3) | flag-to-group knowledge is at every call site; a new extension warning needs three edits | a generated warning table (id, flag, groups, default) next to `diagids.inc` (`DID` already carries a group), state computed once per TU | M | medium | `vcc-diag.py`, `vcc-driver.py`, `vcc-diagcov.py`, `vbench.py gate` |
| Q-diag-13 | L6 | pattern | the level travels as a string and is decoded by its first letter: `pp/diag.c:125` (`dcolor`), `1360` (`level_of`), `cc/diag.c:25`, `330`; passed as `"error"` in `report(c, "error", ...)`, `pp_dinfo.what`, the macros `pp/ppimpl.h:570-572`, `emit/tu.c:27,45,65,280`; next to the `VDL_*` enum.  Two record APIs: `pp_dinfo` (`pp/pp.h:180-203`) and the `vdiag` builder (`pp/pp.h:245-266`) | two ways to do everything; a typo in the string is a note | `VDL_*` everywhere; `pp_dinfo` callers move to the builder | M | low | `vcc-diag.py` |
| Q-diag-14 | L6 | pattern | a note's kind is found from its text: `pp/diagtext.c:121,425` (`strcmp(id, "note_macro_expansion_here")`), `430` (`strncmp(msg, "(skipping ", 10)`, the text made at `pp/diag.c:1298-1299`); the type diff triggers on `strstr(fmt, "type")` (`pp/diagtext.c:618`) | rewording a message changes the layout | a `kind` byte in `vdiag` (macro frame, skipped frames, type pair) | S | low | `vcc-diag.py` |
| Q-diag-15 | L8 | global state | three error counters for one TU: `p->errors` (`pp/diag.c:1815-1818`), `c->errors` (`cc/diag.c:52,66,134,334,425,448,533`), `E->errors` (`emit/tu.c:48,68`), plus `c->ndiag`; read together at `cc/tu.c:333-335` | each reporter must remember which to bump (Q-diag-08 shows they do not) | the engine's count only; `cc`/`emit` ask it | S | low | `vcc-ctests.py`, `vcc-diag.py` (exit status) |
| Q-diag-16 | L10 | dead | `pp/ppimpl.h:483-484` `diag_last`, `diag_lastnote`: no use (replaced by `dlast1`, `dlastlv`, 491-492); wrappers `pp/diag.c:1745` `render_clang`, `pp/diagtext.c:595-598` `diag_narrate`; unused parameter `cap` (`pp/diag.c:1381,1401`); a comment for `include_stack` above `diag_where` (`pp/diag.c:604-605`) | noise | delete | S | none | build |
| Q-diag-17 | L9 | error policy | limits that drop input silently: 4096 pragma transitions and nodes (`pp/diag.c:201,247`), push depth 64 (218), flag names of 59+ bytes (240), message cut at 2048 (1408), help at 512 (1462), 512 SARIF rules (`pp/diagout.c:387,396`) | past the limit a `#pragma GCC diagnostic ignored` stops working: warnings (errors under `-Werror`) appear with no hint why | growable arrays in the arena; or one "too many" record | S | low | a `vcc-stress.py` case (missing) |
| Q-diag-18 | L3 | thread | the engine is single-threaded per `pp`; the emitter's workers serialize on their own `E->emu` (`emit/tu.c:25,43,63`, `arch/a64/emit/call.c:10-16`); renderers change shared state to render: `p->color`, `p->diag_fe` (`pp/diagout.c:156-175`, `pp/diagtext.c:581-588`), `d->emitted` reset after the fact (`pp/diagtext.c:566-568,583-591`) | the contract is nowhere written; a second caller (C++ instantiation on a worker) must rediscover the lock | a lock in the engine, or the rule in `diag.h`; a render context instead of flags flipped and restored | S | low | `vccd-cc.py --bin-dir build/ccs-tsan` |
| Q-diag-19 | L2 | pattern | `p->o->errfd ? p->o->errfd : 2` ten times (`pp/diag.c:1773,1850`, `pp/diagout.c:185,576,591,823`, `pp/diagtext.c:548`, `pp/include.c`, `pp/setup.c` x2); `getenv("VCC_DIAG_DOCS")` per rendered line (`pp/diag.c:1673`); `popen("atos ...")` in the engine (`pp/diag.c:1615-1617`), macOS only, run from a vccd thread | small; the debug aid is in the product path | one `diag_fd(p)`; the knob read once (L8 review); `track_str` to a debug file | S | none | `vcc-diag.py` |
| Q-diag-20 | L2 | duplicate | arena string copy: `pp/diag.c:1371-1378` = `pp/include.c:4-11` = `as/as.c:15-19`; arena vector growth: `pp/diag.c:1005-1012` = `1439-1446` = `1513-1520`; FNV inline: `pp/diag.c:1753-1759` = `driver/ppdriver.c:1794-1797`; escapers: `pp/diag.c:1693-1709` = `lex/lextest.c:43-49`, `pp/diagout.c:30-54` and `vir/print.c:81-85`; `write_file`: `pp/diagout.c:574`, `ld/objcopy.c:499`, `driver/ppdriver.c:407` | for `quality-lib.md` | `lib/` (`varena_strdup`, `lib/vec.h`, `lib/hash.h`) | S | none | build |
| Q-diag-21 | L9 | error policy | `ld/ld.c:3828-3832` prints the version to the process's stdout and calls `exit(0)` inside `ld_main`; vccd answers `--version` first with a second copy of the text (`vccd/vccd.c:1165-1175,1403-1407`, `vccd/caps.c:174`: "Homebrew LLD 23.1.4" against `ld.c:3830` "LLD 23.1.4") | a library entry point that can exit; two version strings | `ld_main` returns; one string | S | low | `vlink-opts.py`, `vccd-test.py` |
| Q-diag-22 | L10 | stale doc | `TODO.md:527-529` lists abort sites in `emit.c`, `cc.c`, `pp.c` (files that no longer exist) and says the remaining aborts are "mmap failures and internal invariants" (Q-diag-01 says otherwise); `docs/RESEARCH-diagnostics.md:141-142` ("the default text stays byte-identical to clang") against `docs/PLAN-diag.md:3-5` and D4; `cc/diag.c:324` "(clang's rendering, pp.c)"; `pp/ppimpl.h:678` | the audit cannot be repeated from the list | rewrite the TODO item from section 3's table | S | none | — |

**Q-diag-04: what the engine needs from pp, and the interface.**  Counted in
the three engine files:

| Need | Uses | What it is |
|---|---|---|
| file-entry table `p->fe[]`, `loc_fent`/`loc_ok`, `fent` fields | 24, 33, 113 | location to file: base, source bytes and size, presumed and spelled name, system flag, the `#include` it came from |
| `#line` marks `p->lm[]`, `file_line` | 8 | presumed line and name |
| `p->o->...` (`pp_opts`) | 58 | about 15 diagnostic options, `errfd`, `cwd`, `argv`, `wargs`, the vccd sink |
| `p->perm` | 7 | the arena records live in |
| engine state in `struct pp` | 71 | record list, pending record, last location, counters, colour, the fatal jump |
| pragma states `dn`, `dtr`, `dstk` | 35 | written by `#pragma` handling, read per location |
| macro table: `mget`, `pmacro`, token arrays, `KW_define`, `vintern_ents` | about 90, all in `pp/diag.c:843-1315,1473-1506` | the macro backtrace and "is this a macro name" |

Only the last row is preprocessor knowledge.  The rest is a source map plus
options.  An interface that lets every tool report:

```
typedef struct vsrcmap {                  /* who owns the sources answers */
    void *ctx;
    bool (*where)(void *, vloc, vwhere *);       /* presumed file/line/col, path, offset, system */
    bool (*text)(void *, vloc, const char **s, uint32_t *size, uint32_t *off);
    vloc (*included_from)(void *, vloc);         /* 0: top */
    void (*expansion)(void *, vdiag *top, vdiag *d);  /* pp only: macro notes; NULL elsewhere */
} vsrcmap;
vdiags *vdiag_open(const vdiag_opts *o, const vsrcmap *m, varena *a);
vdiag  *vdiag_begin(vdiags *, int level, vloc, const char *fmt, ...);   /* then label, fix, note, help */
void    vdiag_emit(vdiags *, vdiag *);
int     vdiag_errors(const vdiags *);  void vdiag_finish(vdiags *);
```

pp implements `vsrcmap` over `fent` (and keeps `diagmacro.c`).  The assembler
implements it over its input buffer (file, line; it has no columns today).
The linker, `ar`, `objcopy` and the driver pass no map: their records have
`loc` none and a subject string ("file.o", "-ftls-model=x"), and the text
renderer prints `TOOL: error: text` as today (`pp/diag.c:1656` already prints
"vcc: " for a record without a file).  `-W` state becomes its own small
object (`diagflags`) that pp's `#pragma` code and both front ends share.

**Q-diag-08: how the six copies differ.**

| Entry | `error_limit` when `-Werror` | `c->errors++` | `c->ndiag++` | clears `c->dfx` when off | `unknown_toks` |
|---|---|---|---|---|---|
| `cc_warnx` (393) | no | no | no | no | no |
| `cc_errorx` (416) | yes | yes | yes | no | yes |
| `cc_warn_begin` (437) | yes | yes | yes | no | no |
| `ext_warn` (521) | yes | yes | yes | yes | no |
| `rt_defer` (546) | no | no | no | no | no |
| `pp_warnx` (pp, 434) | — | — | — | — | — |

So a warning promoted by `-Werror` counts toward `-ferror-limit` and
`c->errors` through three entries and not through the other two, and a
pending fix-it (`c->dfx`) survives a switched-off warning in four of five
(inferred: it would attach to the next diagnostic; not reproduced).

## 5. Names (L5, L6)

| Name | Where | What it is | Proposal |
|---|---|---|---|
| `fatal` | `cc/diag.c:55-68` (45 callers) | an ordinary error, then a jump to the parser's recovery point | `error_recover` / `parse_fail` |
| `fatal` | `pp/ppimpl.h:571` | a "fatal error" record that ends the TU | keep; the only real fatal |
| `efatal` | `emit/tu.c:51-70` (173 callers) | an error, then leave the function being emitted | `eerror_bail` |
| `error`, `warn` | macros in `pp/ppimpl.h:570,572`; static functions in `ld/ld.c:113,128`, `ldmacho.c:69` | five things under libc-like names | `pp_error`, `ld_error` until the engine replaces them |
| `err` | `as/as.c:28` (returns -1, stores), `vccd/ar.c:80`, `vccd/rsh.c:469`, `ld/wasmld.c:45` | four signatures | as above |
| `die` | `driver/ccsnap.c:469` | marks the declaration cache dead; reports nothing | `dc_disable` |
| `diag` | `cc/diag.c:22`, `ld/ld.c:89` | "emit at level" | folds into the builder |
| `report`, `rec` | `cc/diag.c:326`, `311` | error or warning with ranges; `rec` builds a record, and `c->rec` is the recovery point, `recov`, `rec_save` | `diag_ranges`; recovery keeps `recov_*` |
| `idlen_`, `idstr_`, `macro_note_` | `cc/diag.c:575-576`, `pp/expand.c:18` | trailing underscore with no rule behind it | plain names |
| `dstr`, `db_c`, `dcols`, `dtrans`, `dstate_at`, `dpragma`, `wstate`, `mk`, `esc`, `jstr`, `xput`, `xtext`, `xargs`, `xhelp`, `xchain`, `sn_line`, `sn_ls` | `pp/diag*.c` | no common prefix; `VPRIV` ones (`db_c`, `dcols`, `render_head`, `loc_ok`) become globals in the file-by-file build | `diag_` for cross-file, static otherwise |
| `dn`, `dtr`, `dcur`, `dstk`, `nden`, `dhead`, `dpend`, `tpend`, `dlast1`, `dlastlv`, `mdx` | `pp/ppimpl.h:470-492` | engine state as loose fields of `struct pp` | one `struct diag_state` member |
| `pp_dinfo.what` | `pp/pp.h:183` | the level, as a string | `level` (`VDL_*`) |

Prefixes: public API is `pp_diag_*`, but `pp_warnx`, `pp_diagx`, `pp_vdiag`,
`pp_wstate`, `pp_werror_named`, `pp_where`, `pp_src`, `pp_tok_end` break it;
types are `vdiag`, `vdspan`, `vdfix`, `VDL_*`, `VDA_*` (v-prefix) next to
`pp_span`, `pp_fixit`, `pp_drec`, `pp_dsink`, `PP_DIAG_*`, `PP_DSTYLE_*`.
"Fatal" has three meanings (above).  "Note" is both a level (`VDL_NOTE`) and
`cc`'s function `note`.

## 6. Decisions needed

1. **One policy.**  Proposed table; the evidence for today's behaviour is the
   table in section 3.

   | | Standalone tool | Inside vccd |
   |---|---|---|
   | User error (source, object, archive, option) | a record; go on to the limit; exit 1 at the end; never `abort`, never `exit` from a library function | the same record on the request's stream; the request fails; the server goes on |
   | Input-driven exhaustion (arena, region, table or instruction limit) | `vjob_fail`: a fatal record naming the limit; exit 1 | the job ends; a process-wide table marks the server for restart (`vtables_full`) |
   | Out of memory (`malloc`, `mmap` fail) | "TOOL: out of memory", exit 1 | option A: abort, the shim restarts the server (as `vccd/dstore.c:54-62`); option B: fail the job where the memory is the job's |
   | Internal inconsistency | `VBUG(...)`: "internal error" with vcc's file:line, then `abort` | per-job state: an "internal error" record and the job fails (as `delayed_bug`); shared state: `abort` |

   Recommendation: A for process-wide allocations and B through `vjob_fail`
   for per-job ones (linker, assembler, VIR), behind one `lib/xalloc.h`;
   never `exit()` below `main`.  Today `ldmacho.c`, `ld.c:6578` and
   `ld.c:3832` exit, and 89 sites in the tree abort.
2. **Where the engine lives.**  (a) stays in the pp module (PR #134:
   `frontend/diag/`), other tools keep their printf; (b) its own module with
   the `vsrcmap` interface above.  Recommendation: (b), before C++ starts,
   split first by moves (Q-diag-10), then the interface.
3. **Do linker, assembler and driver messages become records** (JSON, SARIF,
   vccd's store, the summary line)?  Their text must stay lld's and
   llvm-mc's where a harness compares it (`vlink-opts.py`).  Recommendation:
   yes, with the text renderer printing each tool's own first line.
4. **Assembler locations.**  The engine's location is an offset in pp's file
   table; the assembler has file and line of text that may come from the
   emitter.  Options: register assembler inputs as files in a shared source
   map, or let a record carry (name, line, col) directly.  Recommendation:
   the first for `.S`/`.s` inputs, none for emitter-made text (an error there
   is vcc's bug: policy row 4).
5. **Warnings as a generated table** (Q-diag-12) or strings as today.  Needs
   a measurement of `wstate` under `-Wall -Wextra` first.
6. **Assertions.**  The tree has no `assert`.  A `VBUG`/`VASSERT` that is on in
   the sanitizer builds and CI, off in the release build?
7. **Silent limits in the engine** (Q-diag-17): grow, or diagnose once.

## 7. Not covered

- `cc/`'s call sites of the engine (about 400) were counted, not read; the
  other `cc` reviews own those files.
- L9 outside the engine was done as the brief defines it: helper definitions
  read, call sites counted by grep; `ld.c`, `as.c`, `vccd.c`, `ppdriver.c`,
  `vir/*.c` were not read in full.  Unchecked `malloc` results were not
  audited beyond the sites named.
- Whether the limits of Q-diag-01 can be reached from input was not tested
  (nothing was built or run); "function too large" is taken at its word.
- The cost of `wstate` and of the two `#define` scans was not measured.
- `pp/diagids.inc`, `bench/gen-diagids.py`, `bench/diag-render.py`,
  `vccd/dstore.c` (the sink's other side; lines 48-120 only),
  `driver/explain.inc`: not reviewed.
- `docs/PLAN-diag.md`: the rules, D1, D3, D4, D7, D8 and the status were
  read, D0, D2, D5, D6 not; `docs/RESEARCH-diagnostics.md`: section 3 only.
- PR #134's tree was not inspected.
