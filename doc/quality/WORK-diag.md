# Work order: diagnostics

The diagnostics engine becomes its own module below the preprocessor, behind
a small source-map interface, so that the assembler, the linkers and the
driver can report through it later.  The evidence is
`doc/review/quality-diag.md` (finding ids `Q-diag-NN`), with
`doc/review/quality-boundaries.md` (`Q-BND-NN`) and `doc/review/quality-lib.md`
(`Q-lib-NN`).  Names follow `doc/STYLE.md`.  Binding: the "Decisions" table
of `doc/PLAN-quality.md`, row "Diagnostics": *its own module below pp, behind
a small source-map interface; assembler, linker and driver report through it
and keep their lld and llvm-mc wording.*

Line numbers below are today's (main at d602dfa2; the reviews' numbers still
hold for the files named here).  If a line has moved, find the same text.

**Starts when** WORK-common steps 1 to 3 are on main (`src/tools/rename-table.py`
exists, `vjob_fail`/`VBUG` are the one way to fail, the library's new names
are in), and the lex and pp work order has merged its last step, so that no
other order holds `frontend/pp/`.

**Owns**
- `src/frontend/diag/` (4 files today) and everything it becomes: the new
  directory `src/diag/` and the new header `src/include/diag/diag.h`.
- New files this order creates in pp: `src/frontend/pp/source-map.c`,
  `src/frontend/pp/source-query.c`, `src/frontend/pp/macro-backtrace.c`.
- For the duration of this order, these edits to pp files the lex and pp
  order has finished with: the diagnostics part of
  `src/include/frontend/pp/pp.h` (lines 134-314) and of
  `src/frontend/pp/ppimpl.h` (the engine fields of `struct pp`, 462 and 468-500;
  the `error`/`fatal`/`warn` macros, 570-572; the prototype block 677-716), and
  the call sites listed in step 4 (`directive.c:313`, `token.c:194`,
  `include.c`, `hcentry.c:346`, `setup.c`, `builtin.c:260`, `macro.c:160`,
  `expand.c:18-41`).
- `src/test/diag/` (snapshots), `src/test/fuzz/fuzz-diag.c`,
  `src/test/fuzz/regress/diag/`, `src/test/fuzz/seeds/diag/`,
  `src/tools/gen-diagids.py`, `src/tools/gen-explain.py`,
  `bench/vcc-diag.py`, `bench/diag-render.py`, `bench/vcc-diagcov.py`,
  `bench/vcc-explain.py`, `bench/vcc-diag-catalog.py`, `bench/sarif-check.py`.
- In `src/Makefile`: the `$(wildcard frontend/diag/*.c)` term of `pp_SRC`,
  the new `diag_SRC` line and its `SPLIT_MODS` entry, and the
  `frontend/diag` words of the `fuzz-pp`/`fuzz-diag` rules.

It edits other modules (`frontend/c/`, `codegen/`, `arch/*/codegen/`,
`driver/`, `server/`, `bin/`) only by script, in steps 2 and 4, and only to
follow a rename or a signature change.  `src/driver/explain.inc` is the
driver order's; `gen-explain.py` keeps writing it where it is.

## What done looks like

- `src/diag/` is a module of its own (`diag_SRC`, one unity TU) that includes
  nothing from `frontend/`: `bench/quality.py` shows no edge `diag -> pp` and
  no include of `ppimpl.h` from `src/diag/`.
- The public interface is `src/include/diag/diag.h` (section "The interface"
  below); `pp.h` keeps only the preprocessor and its source queries.
- pp reports through that interface and provides a `diag_source_map`; a
  tool without a preprocessor can open a context with no map, or with the
  line map of the header, and report.  The conversions of the assembler,
  linkers and driver are their own orders' steps (section "Not in this
  order"), done to the contract in this order.
- One job per file, each with an opening comment; every file-scope function
  is `diag_` (in `src/diag/`) or `pp_` (the three pp files); the engine's
  state is a `struct diag_context` with fields in words; no `vdiag`,
  `VDL_*`, `pp_diag_*` names remain.
- No copy inside the engine of: the fix-it hint line, blank trimming, tab
  expansion, line start/end, colour codes, level names, finding a
  `#define`, location to file entry (Q-diag-09, Q-diag-11).
- Output byte-identical throughout, except the one named step (step 9).

## The interface

The header `src/include/diag/diag.h`, as step 4 writes it.  Each function has
the one-line contract that goes into the header as its comment.

```c
typedef uint32_t diag_location;          /* a point in the source map's location space */
#define DIAG_LOCATION_NONE UINT32_MAX    /* no location: the record prints "TOOL: level: ..." */

enum { DIAG_LEVEL_NOTE, DIAG_LEVEL_REMARK, DIAG_LEVEL_WARNING, DIAG_LEVEL_ERROR, DIAG_LEVEL_FATAL };
enum { DIAG_APPLY_UNSPECIFIED, DIAG_APPLY_MACHINE, DIAG_APPLY_MAYBE, DIAG_APPLY_PLACEHOLDER };
#define DIAG_APPLY_NAME 0x80             /* or'ed in: the fix changes a macro's name, not its body */
enum { DIAG_KIND_PLAIN, DIAG_KIND_MACRO_FRAME, DIAG_KIND_SKIPPED_FRAMES, DIAG_KIND_TYPE_PAIR };
enum { DIAG_FORMAT_TEXT, DIAG_FORMAT_JSON, DIAG_FORMAT_SARIF };
enum { DIAG_STYLE_VCC, DIAG_STYLE_CLANG, DIAG_STYLE_NARRATED };
enum { DIAG_LIMIT_CLANG, DIAG_LIMIT_LLD, DIAG_LIMIT_SILENT };

typedef struct diag_range { diag_location begin, end; } diag_range;     /* [begin, end) */
typedef struct diag_span  { diag_location begin, end; const char *label; } diag_span;
typedef struct diag_fix   { diag_location begin, end; const char *text; uint8_t apply; } diag_fix;
typedef struct diag_record diag_record;  /* today's vdiag, its fields in words, plus `kind` */
typedef struct diag_context diag_context;/* one per job; opaque outside src/diag/ */
typedef struct diag_sink diag_sink;      /* vccd's whole-build store (today pp_dsink) */

/* What a location means, as printed. */
typedef struct diag_position {
	const char *path; uint32_t path_length;      /* presumed name (after #line) */
	const char *spelled_path; uint32_t spelled_path_length; /* as spelled: clang's diagnostics
	                                                ignore -fmacro-prefix-map */
	uint32_t line, column;                       /* 1-based; column 0: the tool has none */
	uint32_t file;                               /* file entry: equal ids share text and include chain */
	bool system_header, main_file;
} diag_position;

/* Who owns the sources answers these; any callback may be NULL. */
typedef struct diag_source_map {
	void *owner;
	/* false: the location has no position (the record prints without file:line) */
	bool (*position)(void *owner, diag_location, diag_position *);
	/* the bytes of the location's file and its offset in them; false: no snippet */
	bool (*text)(void *owner, diag_location, const char **bytes, uint32_t *size, uint32_t *offset);
	/* the #include (.include, macro instantiation) that brought the file in; NONE at the top */
	diag_location (*included_from)(void *owner, diag_location);
	/* pp only: move a record inside a macro expansion to the invocation and add the
	   "expanded from macro" notes as children of top */
	void (*macro_expansion)(void *owner, diag_context *, diag_record *top, diag_record *);
	/* pp only: [begin, end) touches a macro's tokens (a fix-it there is dropped) */
	bool (*range_in_macro)(void *owner, diag_location begin, diag_location end);
} diag_source_map;

typedef struct diag_options {
	const char *tool;              /* prefix of a record without a position: "vcc", argv[0] of vlink */
	int fd;                        /* where text goes; 0: stderr */
	int color, caret, show_id, format, style, unicode, parseable_fixits, patch, fix_apply;
	int error_limit, spell_limit;  /* as -ferror-limit=, -fspell-checking-limit= */
	uint8_t limit_style;           /* DIAG_LIMIT_*: what the record past the limit says */
	bool blank_after_multiline;    /* lld: an empty line after a message of several lines */
	bool threads;                  /* reports may come from several threads: diag_report locks */
	const char *const *warning_options; int warning_option_count;  /* -W, -w in order */
	const char *sarif_file, *html_file, *cwd; char *const *argv;
	diag_sink *sink;
} diag_options;

/* a context whose records live in region; map NULL: no record has a position */
diag_context *diag_open(const diag_options *, const diag_source_map *, vregion *region);
/* a fatal record longjmps here (NULL: diag_report returns, the caller ends the job) */
void diag_fatal_jump_set(diag_context *, jmp_buf *);

/* builder: a record is built on the error path only, then reported once */
diag_record *diag_record_begin(diag_context *, int level, diag_location, const char *fmt, ...);
diag_record *diag_record_begin_va_list(diag_context *, int level, diag_location, const char *fmt, va_list);
void diag_record_label_add(diag_context *, diag_record *, diag_location begin, diag_location end, const char *label);
void diag_record_fix_add(diag_context *, diag_record *, diag_location begin, diag_location end, const char *text, int apply);
void diag_record_help_set(diag_context *, diag_record *, const char *fmt, ...);
void diag_record_art_set(diag_context *, diag_record *, const char *text, size_t length);
diag_record *diag_record_note_add(diag_context *, diag_record *, diag_location, const char *fmt, ...);
void diag_record_flag_set(diag_record *, const char *flag);           /* " [-Wflag]" */
void diag_record_macro_set(diag_record *, const char *spelling, uint32_t length);
/* render (and keep) the record; a fatal record then jumps (diag_fatal_jump_set) */
void diag_report(diag_context *, diag_record *);

/* warnings: the level a warning of flag is reported at, DIAG_LEVEL_WARNING/_ERROR, or -1 (off) */
int diag_warning_level(diag_context *, const char *flag, bool on, const char *const *groups, diag_location);
/* the same for a recoverable error: its default severity survives -w and -Wno-error */
int diag_recoverable_error_level(diag_context *, const char *flag, const char *const *groups, diag_location);
/* report a warning of flag if it is on (clang's " [-Wflag]" / " [-Werror,-Wflag]"); false if off */
bool diag_warning_report(diag_context *, const char *flag, bool on, const char *const *groups, diag_location, const char *fmt, ...);
/* fed by pp: a #pragma clang/GCC diagnostic at location; the state to store per file entry */
void diag_pragma_apply(diag_context *, diag_location, const char *text, const char *end);
uint32_t diag_pragma_state(const diag_context *);
/* fed by pp: leaving a file entered in state (a push without pop ends there) */
void diag_file_leave(diag_context *, uint32_t entry_state);

/* counts and the end */
int diag_error_count(const diag_context *);
int diag_warning_count(const diag_context *);
const diag_record *diag_records(const diag_context *);   /* in order, notes under kids */
void diag_finish(diag_context *);                         /* last JSON record, SARIF, HTML, fix-its */
void diag_summary_print(diag_context *);                  /* "N errors generated." */
const char *diag_level_name(int level);                   /* "error", "fatal error", ... */

/* A source map for a tool without a preprocessor (vasm; later anything that reads text). */
typedef struct diag_line_map diag_line_map;
diag_line_map *diag_line_map_new(vregion *);
/* a buffer (file or macro instantiation); bytes NULL: lines only, no snippet; returns its first location */
diag_location diag_line_map_add(diag_line_map *, const char *path, const char *bytes, uint32_t size, diag_location included_from);
/* the location of line (1-based) in the buffer that starts at first */
diag_location diag_line_map_line(const diag_line_map *, diag_location first, uint32_t line);
const diag_source_map *diag_line_map_source_map(diag_line_map *);
```

**What pp provides.**  `frontend/pp/source-map.c` fills one `diag_source_map`
per `pp`: `position` from the file-entry table `p->fe[]` (`loc_fent`, the
`#line` marks `p->lm[]`, `presumed_name`, `fent.spath`, `fent.sys`), `text`
from `fent.f`, `included_from` from `fent.from`; `macro_expansion` and
`range_in_macro` are `frontend/pp/macro-backtrace.c` (today's `macro_adjust`,
the backtrace of `frontend/diag/diag.c:843-1315` and `fix_in_macro`).
`pp_open` builds `diag_options` from `pp_opts` (the fields listed at
`include/frontend/pp/pp.h:58-80`) and opens the context in `p->perm`; pp's
`#pragma` handler calls `diag_pragma_apply`, file entry stores
`diag_pragma_state` in `fent.dstate`, file exit calls `diag_file_leave`.
`pp_diagnostics(const pp *)` returns the context; the front end and code
generation use it.  The preprocessor's own source queries (`pp_where`,
`pp_src`, `pp_tok_end`, ...) stay `pp_` functions in `source-query.c`.

**How a tool without a preprocessor supplies locations.**
- *vlink, objcopy, ar, the driver:* no map.  `diag_open(&o, NULL, r)` with
  `o.tool` = the name they print today (`argv0` in `ld/ld.c:88`, `"vcc"` in
  the driver); every record is at `DIAG_LOCATION_NONE` and the message
  carries its subject as today (`"foo.o: undefined symbol ..."`).  The text
  renderer prints `TOOL: level: message`, which is lld's line.  vlink sets
  `limit_style = DIAG_LIMIT_LLD`, `error_limit = 20`,
  `blank_after_multiline = true` and `threads = true` (its reports come from
  the `pfor` threads, today under `errlock`).
- *vasm:* a `diag_line_map`.  The input file is one buffer (with its bytes,
  so a snippet can be shown later); each macro or `.rept` instantiation is a
  buffer named `<instantiation>` (llvm-mc's name) whose `included_from` is
  the instantiating line.  A statement error is a record at
  `diag_line_map_line(map, buffer, line)`; column 0 prints
  `path:line: error: message`, as `as/as.c:36` does today; the "while in
  macro instantiation" lines (`as/as.c:2595-2597`) become child notes at the
  instantiating lines; the `"\n  in: ..."` text stays part of the message;
  `error_limit = 20` with `DIAG_LIMIT_SILENT` (`as/as.c:2600`).  Text that
  the emitter made (inline asm) gets no map: an error there is a record at
  `DIAG_LOCATION_NONE` or at the C statement's location, as today
  (`codegen/tu.c:275-282`).

## Steps

Each step is one PR.  Proof for every step unless it says otherwise (load
rules of the worker brief apply: `-j 3`, `nice`):

```
nice -n 10 make -C src -j 3 OUT=$PWD/build/w vcc                     # unity
nice -n 10 make -C src -j 3 VCC_UNITY= OUT=$PWD/build/w-parts vcc     # file by file
bench/vcc-diag.py -j 3 --bin-dir build/w            # snapshots unchanged, never --bless
bench/vcc-mutate.py -j 3 --formats                  # JSON rebuilds the text, SARIF, HTML
bench/vcc-sema.py run -j 3 --bin-dir build/w        # 878/879
bench/vcc-same.py --base DIR -j 3                   # DIR from bench/vbench.py mkbase <branch point>
src/tools/gen-diagids.py --check && src/tools/gen-explain.py --check
bench/vcc-explain.py && bench/vcc-diagcov.py
make -C src fuzz-regress                            # if libFuzzer is installed; else say so
```
`bench/vbench.py gate` is listed where a step can move instructions; it needs
a quiet machine, so the PR says "Proof not run: needs a quiet machine" and
the integrator runs it.

### 1. Split (moves only)

`frontend/diag/diag.c` is split by line range; everything else moves whole.
Use `git mv frontend/diag/diag.c diag/record.c` for the largest piece and cut
the rest out of it, so blame follows.  In this step the files are still
compiled in the pp unity TU and still include `../frontend/pp/ppimpl.h`
(path updated): only `pp_SRC`'s term changes to `$(wildcard diag/*.c)`.  A
`static` function that the split separates from its callers becomes `VPRIV`
with its prototype in the `ppimpl.h` block at 677-716 (the keyword change is
part of the move).

| From | To | What | Why |
|---|---|---|---|
| `frontend/diag/diag.c` 9-54, 80-112, 135-140 | `frontend/pp/source-map.c` | `loc_fent`, `loc_line`, `presumed_name`, `pp_in_system_header`, `pp_in_main_file`, `pp_where`, `loc_ok` | the source map that `__LINE__` and line markers use sits in a file named for diagnostics (Q-diag-10) |
| `frontend/diag/diag.c` 141-190, 456-564, 1872-1875 | `frontend/pp/source-query.c` | `pp_error_limit`, `pp_spell_limit`, `pp_main_eof`, `pp_include_point`, `pp_src`, `pp_invalid_tok`, `pp_spells`, `pp_loc_char`, `pp_src_rest`, `pp_tok_end`, `pp_defined` | the parser's source-text queries are not diagnostics (inventory job c) |
| `frontend/diag/diag.c` 842-1315, 1476-1506, 1574-1600 | `frontend/pp/macro-backtrace.c` | `tok_at` ... `expansion_note`, `fix_in_macro`, `pp_loc_macro`, `pp_macro_defined`, `macro_adjust` | the only part that is preprocessor knowledge (Q-diag-04 table, last row) |
| `frontend/diag/diag.c` 191-455 | `diag/warning-state.c` | `#pragma diagnostic` states, the `-W` state, `pp_wstate`, `pp_wc_fill`, `pp_diag_wlevel`/`elevel`, `pp_werror_named`, `pp_warnx` | job b (Q-diag-10) |
| `frontend/diag/diag.c` 565-602 | `diag/source-line.c` | `cp_width`, `dcols` (display width) | job d; step 6 adds the shared line helpers here |
| `frontend/diag/diag.c` 113-134, 603-841, 1635-1745 | `diag/render-clang.c` | colours, `db_c`, `diag_where`, `include_stack`, `pp_pad_at`, `sn_ls`, `sn_line`, `snippet`, `render_head`, `esc`, `parseable_fixits`, `render_text`, `render_clang` | job e: clang's style |
| `frontend/diag/diag.c` 1316-1475, 1507-1573 | `diag/record.c` | ids (`dfmts`, `fmt_id`), level names, `dstr`, the builder `pp_diag_begin` ... `pp_diag_records`, `kid_add`, `kid_after` | job g |
| `frontend/diag/diag.c` 1601-1634 | `diag/report-site.c` | `track_str` (`VCC_TRACK_DIAG`, `atos`) | job i: a debug aid, not the product path (Q-diag-19) |
| `frontend/diag/diag.c` 55-79, 1746-1871; `diagout.c` 574-620 | `diag/report.c` | `pp_diag`, `pp_diagx`, `dsink_dup`, `pp_diag_emit`, `pp_vdiagx`, `pp_vdiag`, `pp_warnings`, `pp_diag_summary`, `write_file`, `pp_diag_finish` | job h: queue, dedup, count, fatal, the end |
| `frontend/diag/diagtext.c` 1-491, 546-594 | `diag/render-vcc.c` | vcc's style and its flush | one style per file |
| `frontend/diag/diagtext.c` 492-545, 595-598 | `diag/render-narrated.c` | `say_place`, `render_narr`, `diag_narrate` | the narrated style |
| `frontend/diag/diagtext.c` 599-648 | `diag/type-diff.c` | `diag_typediff` | the type diff |
| `frontend/diag/diagout.c` 1-198, 434-457 | `diag/output-json.c` | JSON lines, vccd's record sink | one format per file |
| `frontend/diag/diagout.c` 199-433 | `diag/output-sarif.c` | SARIF | |
| `frontend/diag/diagout.c` 458-573 | `diag/output-html.c` | the HTML page | |
| `frontend/diag/diagout.c` 621-847 | `diag/fixit-apply.c` | `--fix` and the unified diff | fix-its applied to files (inventory, diagout row) |
| `frontend/diag/diagids.inc` | `diag/diagnostic-ids.inc` | generated table | `gen-diagids.py`'s `OUT` and its doc lines follow |

Also in this PR: the paths in `bench/diag-render.py`, `bench/vcc-diag.py:123`,
`bench/vcc-diagcov.py` (`DIRS`, and `PPONLY`'s directory test: the pp macros
are still pp-only), `bench/vcc-explain.py:29`, `src/tools/gen-diagids.py`,
the `fuzz-pp`/`fuzz-diag` Makefile rules, and `doc/STYLE.md`'s prefix table
(row `frontend/diag/` becomes `diag/`: the module is no longer a front end).

Proof adds: `git diff -M --stat` shows only renames and the cut file;
`bench/quality.py --clones 999` reports no new clone.

### 2. Rename (scripted)

The table is checked in as `doc/quality/rename-diag.tsv` and applied with
`src/tools/rename-table.py` across `src/`, `bench/`, `ci/` and live
documents.  Build it from these rules; have the designer check it against
STYLE.md before applying.

| Old | New |
|---|---|
| `pp_diag_begin`, `pp_diag_vbegin` | `diag_record_begin`, `diag_record_begin_va_list` |
| `pp_diag_label`, `pp_diag_fix`, `pp_diag_help`, `pp_diag_art`, `pp_diag_note` | `diag_record_label_add`, `diag_record_fix_add`, `diag_record_help_set`, `diag_record_art_set`, `diag_record_note_add` |
| `pp_diag_flag`, `pp_diag_macro` | `diag_record_flag_set`, `diag_record_macro_set` |
| `pp_diag_emit` | `diag_report` ("emit" is the code generator's word, STYLE glossary) |
| `pp_vdiag`, `pp_vdiagx`, `pp_diagx`, `pp_diag` (static) | `diag_report_va_list`, `diag_report_info_va_list`, `diag_report_info`, `diag_report_format` |
| `pp_warnx`, `pp_wstate`, `pp_werror_named` | `diag_warning_report`, `diag_warning_state`, `diag_warning_error_named` |
| `pp_diag_wlevel`, `pp_diag_elevel` | `diag_warning_level`, `diag_recoverable_error_level` |
| `pp_diag_pragmas`, `pp_diag_npragma`, `pp_diag_wargs`, `pp_diag_nenable_ref` | `diag_pragma_any`, `diag_pragma_transition_count`, `diag_warning_options`, `diag_warning_enable_count_ref` |
| `pp_diag_records`, `pp_diag_finish`, `pp_diag_summary`, `pp_errors`, `pp_warnings` | `diag_records`, `diag_finish`, `diag_summary_print`, `diag_error_count`, `diag_warning_count` |
| `pp_error_limit`, `pp_spell_limit`, `pp_diag_art_on` | `diag_error_limit`, `diag_spell_limit`, `diag_art_style` |
| `pp_diag_pc`, `pp_diag_next_id`, `pp_diag_fmt_id`, `pp_diag_level` | `diag_report_site`, `diag_next_id`, `diag_format_id`, `diag_level_name` |
| `pp_where`, `pp_in_system_header`, `pp_in_main_file` | `pp_location_position`, `pp_location_in_system_header`, `pp_location_in_main_file` |
| `pp_src`, `pp_src_rest`, `pp_tok_end`, `pp_loc_char`, `pp_spells` | `pp_source_text`, `pp_source_text_rest`, `pp_token_end`, `pp_location_character`, `pp_location_spells` |
| `pp_main_eof`, `pp_include_point`, `pp_invalid_tok` | `pp_main_file_end`, `pp_include_location`, `pp_invalid_token_report` |
| `pp_defined`, `pp_macro_defined`, `pp_loc_macro` | `pp_macro_id_is_defined`, `pp_macro_name_is_defined`, `pp_location_is_macro_name` |
| types `vdiag`, `vdspan`, `vdfix`, `pp_span`, `pp_fixit`, `pp_dinfo`, `pp_drec`, `pp_dsink` | `diag_record`, `diag_span`, `diag_fix`, `diag_range`, `diag_fixit` (merged into `diag_fix` in step 5), `diag_info`, `diag_sink_record`, `diag_sink` |
| `VDL_*`, `VDA_*`, `VDA_NAME`, `PP_DIAG_TEXT/JSON/SARIF`, `PP_DSTYLE_*` | `DIAG_LEVEL_*`, `DIAG_APPLY_*` (`VDA_UNSPEC` -> `DIAG_APPLY_UNSPECIFIED`), `DIAG_APPLY_NAME`, `DIAG_FORMAT_*`, `DIAG_STYLE_*` |
| record fields `kids`, `klast`, `msg`, `ms`, `mlen`, `nspan`, `nfix`, `cspan`, `cfix`, `dup` | `children`, `last_child`, `message`, `macro_spelling`, `macro_spelling_length`, `span_count`, `fix_count`, `span_capacity`, `fix_capacity`, `duplicate` |
| static and `VPRIV` functions in `src/diag/` | `diag_` + noun + verb: `dstr` -> `diag_string_copy`, `db_c` -> `diag_text_repeat`, `dcols` -> `diag_display_columns`, `cp_width` -> `diag_code_point_width`, `dtrans` -> `diag_pragma_transition_add`, `dstate_at` -> `diag_pragma_state_at`, `dpragma` -> `diag_pragma_level`, `wstate` -> `diag_warning_state_compute`, `sn_line`/`sn_ls` -> `diag_snippet_line`/`diag_snippet_line_start`, `snippet` -> `diag_snippet_render`, `render_head` -> `diag_head_render`, `render_text` -> `diag_clang_render`, `mk` -> `diag_mark_make`, `esc` -> `diag_text_escape`, `jstr`/`jcstr` -> `diag_json_string_append`/`diag_json_cstring_append`, `json_rec` -> `diag_json_record_write`, `sarif_*` -> `diag_sarif_*`, `html_*` -> `diag_html_*`, `fix_*` -> `diag_fixit_*`, `loc_ok` -> (pp) `pp_location_file_entry_valid` |
| static and `VPRIV` functions in the three pp files | `pp_` + noun + verb: `loc_fent` -> `pp_location_file_entry`, `loc_line` -> `pp_location_line`, `presumed_name` -> `pp_presumed_name`, `expansion_at` -> `pp_macro_expansion_at`, `expansion_note(_scan)` -> `pp_macro_expansion_note_add(_by_scan)`, `mdx_update` -> `pp_macro_definition_index_update`, `mdef_body` -> `pp_macro_definition_body`, `xchain`/`xput`/`xtext`/`xargs`/`xhelp` -> `pp_backtrace_chain`/`_text_append`/`_text`/`_arguments`/`_help`, `macro_adjust` -> `pp_macro_backtrace_add`, `fix_in_macro` -> `pp_range_in_macro`, `obj_macro_at` -> `pp_object_macro_at` |
| files `include/frontend/pp/pp.h`'s diagnostic block | unchanged here (moves in step 4) |
| include guards of new headers | `VCC_DIAG_NAME_H` |

Struct `pp` engine fields are renamed in step 4, when they move.  Before
applying, check that no new name is already used anywhere (`git grep -w`).

Proof adds: `git grep -wE 'vdiag|VDL_[A-Z]+|pp_diag_[a-z_]+|pp_warnx|pp_vdiag'`
returns nothing outside `doc/` status history.

### 3. Comments

An opening comment on every file of `src/diag/` and the three pp files
(STYLE "Comments"): what it does in one sentence, then what a reader must
know.  The module overview is the opening comment of
`src/include/diag/diag.h` (written in step 4); its draft is the review's
"What the module is for" (section 2), corrected for: the module is
`src/diag/`; "emit" is now "report"; one context per job, not per `pp`.  Fix
the stale lines of Q-diag-22: `TODO.md:527-529` (rewrite the item from the
review's section 3 table), `doc/RESEARCH-diagnostics.md:141-142`, the
comment block of `ppimpl.h:678`.  Delete the dead code of Q-diag-16
(`diag_last`, `diag_lastnote` in `ppimpl.h:483-484`; the wrappers
`render_clang`, `diag_narrate`; the unused `cap` parameter of
`vec_suffix`; the stray comment above `diag_where`).

Proof: as the common proof (identical output).

### 4. The interface (identical output)

- `struct diag_context` in `src/diag/diag-internal.h` takes the engine's
  state out of `struct pp` (`ppimpl.h:462`, `468-500`), with words for fields:

  | Old (`struct pp`) | New (`struct diag_context`) |
  |---|---|
  | `dn`, `ndn` | `pragma_nodes`, `pragma_node_count` |
  | `dtr`, `ndtr` | `pragma_transitions`, `pragma_transition_count` |
  | `dcur`, `dstk`, `ndstk` | `pragma_state`, `pragma_stack`, `pragma_stack_depth` |
  | `nden` | `pragma_enable_count` |
  | `color`, `diag_fe` | `color`, `last_file` (step 7 removes the flipping) |
  | `dhead`, `dtail`, `dpend`, `tpend` | `records`, `records_last`, `json_pending`, `text_pending` |
  | `dlast1`, `dlastlv` | `last_location_plus_one`, `last_level` |
  | `dtrack`, `dfinished`, `errors`, `warns` | `track_report_site`, `finished`, `error_count`, `warning_count` |
  | `fatal_jb` (diagnostic use only) | `fatal_jump` |

  `mdx`, `nmdx`, `cmdx`, `mdx_nfe` stay in `struct pp` (the `#define`
  index is pp's) as `macro_definitions`, `..._count`, `..._capacity`,
  `..._file_count`.
- The public declarations move from `pp.h:134-314` to
  `src/include/diag/diag.h` in the form of "The interface" above;
  `pp.h` includes `diag/diag.h` and keeps `pp_diagnostics()` and the source
  queries.
- Every `diag_*` function takes `diag_context *` instead of `pp *`.  A script
  rewrites each call `diag_NAME(EXPR,` whose first argument is a `pp *` into
  `diag_NAME(pp_diagnostics(EXPR),` (front end: `c->p`; code generation:
  `e->c->p`, `c->p`); the list of functions is the ones whose first parameter
  was `pp *` or `const pp *` after step 2.  Call sites today: 532 in
  `frontend/c/`, 190 in `frontend/pp/`, 14 in code generation, 18 in
  `driver/`, 39 in `server/`, `bin/`, `src/test/fuzz/`.
- `frontend/pp/source-map.c` implements `diag_source_map` as described under
  "What pp provides"; `pp_open` (`setup.c`) opens the context and sets the
  fatal jump (`setup.c:157,164,1008`); `directive.c:313` calls
  `diag_pragma_apply`; `include.c:358` and `hcentry.c:346` store
  `diag_pragma_state()` in `fent.dstate`; `token.c:194` calls
  `diag_file_leave`; `include.c:603-604,685-686,977-978,987-988` keep their
  own `fatal_jb` (pp's, not the engine's).
- `pp_wc_fill`/`pp_wc_maybe` (`builtin.c:260`, `macro.c:160`) stay pp's, in
  `source-query.c`, calling `diag_warning_state`.
- `src/diag/` no longer includes `ppimpl.h`.  `src/Makefile`:
  `diag_SRC = $(wildcard diag/*.c)`, `diag` added to `SPLIT_MODS`, the
  `diag/*.c` term removed from `pp_SRC`.
- `diag_line_map` (`src/diag/line-map.c`) and a unit test of it,
  `src/test/diag-unit/line-map.c`, run by `bench/vcc-diag.py --unit` (a
  context with no map and one with a line map: `TOOL: error: x`,
  `a.s:3: error: x`, a note at an instantiation, the limit styles).

Proof adds: `bench/quality.py` (no edge `diag -> pp`, no include of
`ppimpl.h` from `src/diag/`), `bench/vcc-driver.py`, `bench/vccd-test.py`,
`bench/vcc-diff.py cmp -j 3 --kernel` (pp's `-E` path calls the source map),
`bench/vbench.py gate` (quiet machine).

### 5. One record API, the level as a number (identical output; Q-diag-13)

- `diag_info` (today `pp_dinfo`) loses `what`: callers build with
  `diag_record_begin` and the helpers instead; `diag_fixit` and `diag_fix`
  are one type.
- The level is `DIAG_LEVEL_*` everywhere: `dcolor` and `level_of`
  (decode by first letter, `render-clang.c`, `record.c`) go; the pp macros
  `error`/`fatal`/`warn` (`ppimpl.h:570-572`) pass a level.  Outside the
  owned files a script rewrites the string literals at call sites of the
  changed functions: `"error"` -> `DIAG_LEVEL_ERROR`, `"warning"` ->
  `DIAG_LEVEL_WARNING`, `"note"` -> `DIAG_LEVEL_NOTE`, `"fatal error"` ->
  `DIAG_LEVEL_FATAL` (`codegen/tu.c:29,47,67,284`, `frontend/c/diag.c`'s
  `report(c, "error", ...)` callers); a literal it cannot map stops the
  script.

### 6. One copy of each piece (identical output; Q-diag-09, Q-diag-11)

- `diag/source-line.c` gets: line start/end (`diag.c` 643-648, 764-769 =
  `diagtext.c` 42-58 = `diagout.c` 698-714), blank trimming (`diag.c` 689-697
  = 708-715 = `diagtext.c` 314-322), tab expansion (`diag.c` 661-673 =
  `diagtext.c` 162-175), the fix-it hint line (`diag.c` 812-835 =
  `diagtext.c` 275-295), one colour table (`diag.c` 121-133 = `diagtext.c`
  25-30), one level-name table (`diag.c:1356` = `diagout.c:64`), the span and
  fix-it JSON writer (`diagout.c` 130-140 = 140-150).  Today's numbers are
  of the old files: find the text in the step 1 files.
- Finding a `#define`: `pp_macro_definition_body` (the sorted index) serves
  all three; `expand.c:18-41` (`macro_note_`) and
  `pp_macro_expansion_note_add_by_scan` (old `diag.c:916-969`) go.
- `setup.c:808-818` (`tok_col`) calls `pp_location_file_entry` instead of its
  copy.
- `fix_patch`'s line arithmetic computes line starts once per file, not per
  hunk (`diagout.c:698-714,754-776`).

Proof adds: `bench/vcc-diff.py text -j 3` and `lines -j 3` (`tok_col` is on
the `-E` path); `bench/quality.py --clones 999` shows the runs of Q-diag-09
gone; `bench/vbench.py gate` (quiet machine).

### 7. Kinds, threads, knobs (identical output; Q-diag-14, -18, -19)

- `diag_record.kind` (`DIAG_KIND_*`): set where the record is made
  (macro frame, "(skipping N expansions...)" at old `diag.c:1298-1299`, type
  pair); `render-vcc.c` tests the kind instead of `strcmp(id,
  "note_macro_expansion_here")` (old `diagtext.c:121,425`), `strncmp(msg,
  "(skipping ", 10)` (430) and `strstr(fmt, "type")` (618).
- `diag_report` takes `ctx->lock` when `options.threads` (cold path only);
  renderers get a `diag_render` argument (colour, last file) instead of
  flipping `ctx->color`/`last_file` and restoring them, and `emitted` is not
  reset after the fact (old `diagout.c:156-175`, `diagtext.c:566-591`).  The
  rule goes into `diag.h`: one context per job; reports from several threads
  need `threads`.
- One `diag_output_fd(ctx)` for the ten `errfd ? errfd : 2`; `VCC_DIAG_DOCS`
  read once at `diag_open` (old `diag.c:1673`); `report-site.c` keeps
  `popen("atos")` but only under `VCC_TRACK_DIAG`, as today, and says so in
  its opening comment.

Proof adds: `bench/vccd-cc.py --bin-dir build/src-tsan` (`make -C src
vcc-tsan`); a worker without a TSan build says so in the PR and the CI tsan
job covers it.

### 8. Limits that drop input (Q-diag-17; changes behaviour past a limit)

The pragma transitions and nodes (4096, old `diag.c:201,247`), the push
depth (64, 218), the flag name length (59, 240), the message cut at 2048
(1408), help at 512 (1462), 512 SARIF rules (`diagout.c:387,396`): grow in
the context's region instead (the review's first option; growing is what
every other table of the engine does).  Below each old limit nothing
changes.

Proof adds: `bench/vcc-stress.py` with new cases (5000 `#pragma clang
diagnostic` lines then a warning that must stay ignored; push depth 100; a
70-byte flag name; a 3000-byte message) which fail before and pass after;
`bench/vcc-stress.py --vccd`.

### 9. Out of memory and unchecked allocations (Q-diag-03)

The engine's unchecked allocations (`calloc` in old `diag.c:677`, `realloc`
in `diagout.c:637`, `calloc` in `diagtext.c:178-180`) fail through
`vout_of_memory()` (WORK-common step 3).  Only the out-of-memory path
changes.

Proof: common proof plus the ASan suite: `bench/vcc-diag.py --bin-dir
build/src-asan` (`make -C src vcc-asan`; the CI asan job if not built
locally).

## Not in this order

- Converting the other reporters (Q-diag-05, -06, -07, Q-BND-13): each is a
  step of the order that owns the file, done to this contract:

  | Tool, file | Today | Becomes | Text that must stay | Proof |
  |---|---|---|---|---|
  | vlink, `ld/ld.c:89-134` | `error`/`warn` to `errf`, limit 20 | records, no map, `tool = argv0`, `DIAG_LIMIT_LLD` | lld's lines byte for byte | `bench/vlink-opts.py`, `bench/vlink-bad.py` |
  | `platform/darwin/ldmacho.c:66-79` | `error` to `errf` | the same context as vlink | as today | `bench/vlink-opts.py` |
  | `ld/objcopy.c:45-60`, `utils/ar.c:80-88`, `utils/rsh.c:469-476` | own `err` | records, no map | as today | `bench/vobjcopy-cmp.sh`, `bench/var-cmp.py`, `bench/vsh-test.py` |
  | vasm, `as/as.c:28-42,2588-2601`; `.warning` at `as/as.c:2425-2427` | `a->msg` text; stderr | records with a `diag_line_map` | llvm-mc's `file:line: error:` | `bench/vas-obj.py`, `bench/vas-corpus.py`, a new `src/test/diag` case for a `.S` error |
  | `obj/obj.c:27-34` `elf_error` | first error, no location | records, no map | as today | `bench/vas-obj.py` |
  | driver, `driver/ppdriver.c` (about 27 `dprintf(errfd`) | text, three prefixes | records, no map, `tool = "vcc"` | `bench/vcc-driver.py`'s comparison | `bench/vcc-driver.py` |
  | code generation, `codegen/tu.c:25-70` | its own lock `E->emu` | the context with `threads` | as today | `bench/vcc-diag.py` |

- Warnings as a generated table (Q-diag-12, review decision 5): needs a
  measurement of `wstate` under `-Wall -Wextra` first.
- The front end's six "report a warning of a flag" entries (Q-diag-08) and
  its error counters (Q-diag-15): `frontend/c/diag.c` is WORK-cc-decl's.
- The policy itself (Q-diag-01, -02, -03 outside the engine): WORK-common
  step 3; each module's own aborts in its order.
- `ld_main`'s `exit(0)` for `--version` and the two version strings
  (Q-diag-21): the ld order.

## Bugs, not in this order

Each needs a test and a fix in its own PR (PLAN-quality decision "Bugs from
the reviews"); none was fixed by PR #137 (checked on main at d602dfa2).
- `driver/ppdriver.c:1242`: the `-ftls-model=` error goes to the process's
  stderr; under vccd it never reaches the user (Q-diag-05).
- `as/as.c:2425-2427`: `.warning` is printed to the process's stderr: lost
  under vccd (Q-diag-06).
- The assembler's text reaches the user as `vcc: error: file:line: error:
  msg` (`driver/ppdriver.c:1468-1469,1937-1938`): two level words, not
  parseable by an editor (Q-diag-06).
- `frontend/c/diag.c`: a pending fix-it (`c->dfx`) survives a warning that is
  switched off in four of the five entries and may attach to the next
  diagnostic (Q-diag-08; inferred, not reproduced).

## Report

Per step: the PR, the proof commands with their results (numbers), anything
this order got wrong.  The designer checks the result against STYLE.md and
"What done looks like", not the diff line by line.

## Adoption of common/ facilities

Only after WORK-common step 5 has merged; one PR, identical output (common
proof).  Sites by file (old file names; find them in the step 1 files):

| File | Site | Count | Facility |
|---|---|---|---|
| `frontend/diag/diag.c` | arena vector growth 1005-1012, 1439-1446, 1513-1520 | 3 | `VVECTOR_NEED_IN` / `VVECTOR_APPEND_IN` (`common/vector.h`) |
| `frontend/diag/diag.c` | FNV-1a inline 1753-1759 (`DH`, the dedup key) | 1 | `vhash_*` (`common/hash.h`; the key is not stored on disk) |
| `frontend/diag/diag.c` | arena string copy `dstr` 1371-1378 (= `frontend/pp/include.c:4-11`, `as/as.c:15-19`) | 1 | the region's string copy, if WORK-common adds one; else stays |
| `frontend/diag/diagout.c` | `write_file` 574 | 1 | `vfile_write_all` (`port/file.h`) |
| `frontend/diag/diagout.c` | `realloc` doubling of `fedits` 637 | 1 | `VVECTOR_APPEND` |
| `frontend/diag/diagtext.c` | `calloc` 178-180 | 2 | `VVECTOR_NEED` or the region |
| `frontend/diag/diag.c`, `diagout.c`, `diagtext.c` | `vbuf_*` calls (49, 123, 63) | 235 | `vbuffer_*` (renamed by WORK-common's script; nothing to do unless step 4 of WORK-common changed the failure mode: then check each `diag_finish` write) |

No LEB128, byte load/store, string table or file mapping in this module.

## Status

Step 1 (split) done, on branch `quality/diag-step1`.  Where the file map did not
fit the code, or the step needed a decision:
- `git mv diag.c diag/record.c` was not used: record.c is one of the smaller
  pieces (236 lines of 1875), so blame would not follow a rename anyway.  All
  pieces are new files cut by line range from the old three.
- Separated `static` functions became `VPRIV` with prototypes in `ppimpl.h`:
  `jstr`, `jcstr`, `html_page`, `sarif_log`, `dsink_records`, `level_of`,
  `kid_add`, `kid_after`, `render_clang`, `track_str`, `render_narr`, `lstart`,
  `lend`, `fix_in_macro`, `macro_adjust`.
- `lvname` (static array in record.c) is read by render-clang.c: the one use
  became `pp_diag_level(d->level)`, the same value, so no shared array.
- `DC_OLD`/`DC_NEW` moved with their only user, `type-diff.c`.
- The comment of `diagout.c` (lines 1-22, JSON and SARIF both) opens
  output-json.c whole; step 3 rewrites it per file.
- `gen-diagids.py --check` says out of date on origin/main too (before this
  step); not touched.
- `bench/vcc-same.py` reports 7 `-E` differences (arm_neon.h, stdatomic users):
  only the absolute path of `src/headers/` in the line markers, which differs
  because base and new are built in different directories.
