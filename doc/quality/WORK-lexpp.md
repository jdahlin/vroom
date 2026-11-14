# Work order: lexer, preprocessor and header cache

The second module pass of `PLAN-quality.md` (after the shared library,
`WORK-common.md`).  The evidence is `doc/review/quality-lexpp.md` (finding ids
`Q-lexpp-NN`, written on the old paths: `ccs/vcc/pp` is `src/frontend/pp`,
`ccs/vcc/lex` is `src/frontend/lex`, the diagnostics files are in
`src/frontend/diag`).  Names follow `doc/STYLE.md`.

**Owns** every file under `src/frontend/pp/` and `src/frontend/lex/` except
the generated `*.inc` tables, `src/include/frontend/pp/` and
`src/include/frontend/lex/` once step 4 or later needs them, and the `pp_SRC`
and `LEX_SRC` lists in `src/Makefile`.  It does not touch `src/frontend/diag/`,
`src/common/`, `src/port/` (their own orders), nor the callers of `pp.h` and
`lex.h`, except to follow a rename by script.

## What done looks like

- One job per file, each opening with a one-sentence comment; no section
  label names a file that does not hold the code (Q-lexpp-21).
- `ppimpl.h` holds the preprocessor's core types, `struct pp` and the inline
  code the hot path needs; the header cache's types are in their own header.
- One language-mode mechanism instead of four file rewrites (Q-lexpp-01).
- One concurrent table and one shared region, from `common/` (Q-lexpp-05).
- Lexing knowledge lives in the lexer (Q-lexpp-06).
- Every name follows STYLE.md.

## Steps

Each step is one PR.  Proof for every step unless it says otherwise:
`make -C src -j 6 vcc` both unity and `VCC_UNITY=`, `bench/vcc-diff.py batch
--cmp --kernel`, `bench/vcc-ctests.py --suite vcc`, `bench/vcc-diag.py`,
`make -C src fuzz-regress`, `bench/vcc-same.py --base` byte-identical against a
base built before the step, and, because this is the kernel build's hottest
module (53 % of a TU), `bench/vbench.py gate` within 1 %.

### 1. Split (moves only, this PR)

No function, type or variable is renamed and no body changes, except that a
`static` function now called from a second file becomes `VPRIV` (it is
`static` in the unity build, so the code is the same).  The hot path stays
where it is textually: `pp_next`, `raw`, `raw_plain`, `next_exp` keep their
bodies, their `inline`/`noinline` attributes and, for the inline ones, their
place in `ppimpl.h`.

`setup.c` (1089 lines), `text.c`, and the `-dM`/`-dD` half of `macro.c`:

| From | To | Why |
|---|---|---|
| `setup.c` `vm`, builtin table, `pp_open`, `pp_close`, `pp_errors`, `pp_set_fatal`, `pp_files` | `translation-unit.c` | opening, closing and describing one TU (Q-lexpp-03's `open.c`) |
| `setup.c` `pp_next` and its slow paths, `hc_mode`, `hc_env`, `pp_hold_tmp`; `next_tok` to `ppimpl.h` | `next-token.c` | the pull interface; nobody found it in `setup.c` |
| `setup.c` `pp_top_events`, `pp_ntok`, `pp_skipped`, `pp_open_hit`, `pp_hit_ntok`, `pp_hit_regions`, `drcov_*`, `pp_skip_top`, `pp_skip_hit` | `header-skip.c` | what the C front end asks of header-cache hits: events, regions, skipping a hit's remaining tokens (the word "region" here is the header cache's; the glossary's `region` is a bump allocator: renamed in step 5) |
| `setup.c` `out_nl` to `pp_text`, `ucn_utf8`; all of `text.c` | `text-output.c` | the `-E` printer: line markers, comments, spacing, the output buffer |
| `setup.c` `pp_hc_stats`; `hstat` | `header-cache-statistics.c` | counters of the header cache |
| `macro.c` `-dM`, `-dD` (lines 184-337) | `macro-dump.c` | printing macros is not the macro table |
| `include.c` from `#embed (C23)` to the end | `embed.c` | a directive with its own parameters, search and output (`pstrdup` becomes `VPRIV`) |

`ppimpl.h` (1125 lines):

| From | To | Why |
|---|---|---|
| the header cache's types (`hread` to `hkidr`, `htok_get`, `hstat_t`, `HC_MAX*`, `hkey`, `hlev`, `rec_spare_t`) | `header-cache-internal.h`, included by `ppimpl.h` where the text was | `struct pp` needs them complete; the other files do not need them in their eyes (Q-lexpp-04) |
| `struct incent` | `include.c` | only `include.c` reads its fields |
| `dollar_split` | `token.c` (prototype stays) | a cold function defined in a header |
| `touch_write_kid` | `header-cache-record.c` | only the recorder calls it |
| section labels | corrected | they named files that do not hold the code |

`hcache.c`, `hcentry.c`, `hcrec.c` become six files by job (Q-lexpp-09; the
glossary reserves "serve" for handing recorded tokens to the preprocessor, so
the review's `replay` names become `serve`):

| New file | Holds |
|---|---|
| `header-cache-key.c` | option signatures, the key table, the `#pragma once` set |
| `header-cache-store.c` | deduplicated chunks and blobs, `hsalloc` |
| `header-cache-validate.c` | `hc_top`, the read checks, `hc_valid` |
| `header-cache-serve.c` | token and text serving: `push_replay`, `replay_*`, lazy bodies, `hc_apply`, `hc_try`, `emit` |
| `header-cache-record.c` | what a region reads and writes: `hcrec.c`, `rec_tok`, child entries, `hc_begin`, the spare buffers |
| `header-cache-publish.c` | `hc_end` and the entries it builds, text and tokens |

Not cut, with the reason: see "Status".

### 2. Language modes (Q-lexpp-01; changes output, named as such)

Decision first: a mode word in `lex_mode` (digraphs, trigraphs, `//`, digit
separators, raw strings) instead of the four `vlex_*` rewrites, cached per
mode; fixes `1'000` in `-E` and `#x`, makes `#embed` of `1'0` right
(Q-lexpp-02).  Validated by `vcc-diff.py text`, `lexdiff.py`, fuzz-lex (NEON
and scalar equal), the gate; the output change is listed in the PR.

### 3. One table, one region (Q-lexpp-05, -07, -08, -16)

After the shared library offers them: the insert-only concurrent table
replaces `inc_tab`, `canon_tab`, `hkey_tab`, `hck_tab`, the signature list and
the lexer's table; one shared bump region replaces `salloc`, `hsalloc`,
`dalloc`; recording buffers go through the job-region slots (fixes the leak
of `rec.kid`/`rec.kex` in `pp_close`).  Proof adds a TSan batch and
`vccd-cc.py`.

### 4. Lexing outside the lexer (Q-lexpp-06, -10, -11, -12, -23)

`lex` exports spelling a `ptok`, lexing one token from bytes and skipping a
comment; `token.c`'s `splice_at`/`pp_spell`, `expand.c`'s `classify`, the
comment scanners of the printer, the number and escape readers of `ifexpr.c`
use them.  The validity rule of the header cache (three copies) becomes one
iterator.

### 5. Rename (scripted, as `WORK-common.md` step 2)

The table is `doc/quality/rename-lexpp.tsv` (`old new kind where comment`:
functions, variables, fields, types, constants and macros of `pp/` and
`lex/`; kind `public` marks what `pp.h`, `lex.h` or `dircache.h` export, so
it touches other modules; the fifth column is the comment text to write where
the meaning took reading to find).  The grouping of `struct pp` is in
`doc/quality/rename-lexpp-groups.md`.  The script applies whole-word renames
across `src/` (fields by struct: `p->old` and `.old` only where the owning
struct is that one; `where` names it), `bench/`, `ci/` and
`src/tools/gen-ppdata.sh` (the generated tables name `has_tab`).  The
diagnostics files read `struct pp` fields and constants (`diagnostic_*`,
`T_*`): the script edits them, a worker owning them must not have open work.
Public lines are applied last and by themselves (the `T_*` kinds and `vtok`,
`vlex_file` touch every module).  Names with no good word yet, to be decided
when the table is applied: `hent.span`, `struct pp.top_rec`, `rst`, `hm`,
`pend`, the `drcov` family (`pp_coverage_*`), `lex_mask_pack`, `pp_address_space_reserve`
(`vm`), `pp_if_*` for the `#if` parser, and the header cache's `region`
(glossary: bump allocator).

`doc/quality/rename-lexpp.tsv`: every file-scope function gets `pp_`/`lex_`,
`ppimpl.h` becomes `pp-internal.h`, `hent`, `hkey`, `ctx`, `fent`, `pmacro`
get module prefixes and words, `struct pp` fields get words, the
header-cache "region" becomes something other than the glossary's region.
Opening comment of `pp.h` becomes the module overview (draft: review section
2).

### 6. Comments, ratchet

Fix the stale lines of Q-lexpp-21, `lex.h`'s masks, `AGENTS.md`'s layout
paragraph; bless `bench/quality.py` for `lex/` and `pp/`.

## Not in this order

Q-lexpp-13 to -20, -24 where they belong to another module (the diagnostics
API in `pp.h` is `quality-diag`'s; the options record is
`quality-driver-server`'s).

## Status

`make -C src fuzz-regress` does not build on this branch:
`test/fuzz/fuzz-pp.c:119` sets `o.nsys`, a field `pp_opts` no longer has
(`pp.h:51` has `nsys1`).  The same break is on origin/main (the file and
`pp.h` are identical there); it is not from this pass.  Fix it before relying
on fuzz-regress as a proof in steps 2-5.

Step 1 done (PR "pp: one job per file (moves only)").  Left in place, on
purpose:

- `lex.c` stays one file.  The review's `lexcache.c` (token cache, file
  reading, mode rewrites, read-ahead, from line 1540) calls `lex_mode`, which
  is `static` and specialised by inlining (`vlex` passes `digraphs` = true as a
  constant); exporting it would change the hot loop's code.  The cut is made
  in step 2, when the four rewrites go and `lex_mode` takes a mode word.
- `file.c` (skeleton and `salloc`) and `directive.c`'s `parse_define` keep
  their place: renaming to `skeleton.c` and moving `parse_define` to `macro.c`
  belong to step 3 (`salloc` goes to `common/`) and step 5.
- `struct pp` is not cut into sub-structs: it changes every field access
  (not a move); step 5 does it with the field rename.
- `vm()` stays in `translation-unit.c`: it is the TU's address-space
  reservation until step 3 replaces it.
- `header-cache-internal.h` keeps the inline `htok_get` with the types (hot
  path, used by `pp_next`); the prototypes of the header cache's functions
  stay in `ppimpl.h` because they take `pp *`.

Step 5 done for everything but the struct fields that need the field-use mode
(PR "lex, pp, diag: names per STYLE.md, by table").  `rename-table.py` got a
scoped mode for it (`--scoped src/frontend/pp,src/frontend/lex,src/diag
--users-of ...`, see its docstring): a row that is not `public` applies only
under those directories (`in:FILE` narrows it, `also:PATH` adds a bench script),
a `public` row only to files that include `pp.h`, `lex.h` or `dircache.h`
directly or through another header (so `T_*` of the assembler's or the VIR
parser's own enums stay), no row renames a word after `#` (directives named
`include`, `define`, `error`) or after `->`/`.` unless it is a field row, and
comments get only names that cannot be English words (underscore, upper case,
`name(`).  The table:

- applied: 284 + 31 functions, 61 + 3 variables, 142 + 73 constants, 39 + 3
  types, 7 macros, the file row (`ppimpl.h` is `pp-internal.h`, moved by hand:
  it is not under `include/`), and 84 fields whose old name is used only as
  that field in the whole tree (checked by script: every other occurrence is a
  member access or the one declaration);
- corrected: `struct`/`enum` rows keep their keyword; `vlex`, `pp_stat_try`,
  `pp_stat_skip` are `public` (used by the driver, the fuzzers and `lextest`);
  `builtin` (a function and a `struct pp` field), `base`, `used`, `bi` and
  `pidx` (struct members of the same name elsewhere) are narrowed with `in:`;
  identity rows (`counter_delta`, `text_started`) are gone;
- deferred, collected at the end of `rename-lexpp.tsv` with kind
  `field,deferred` (229 rows): struct fields with short or shared names (`n`,
  `len`, `fe`, `off`, `src`, ...) that a whole-word run cannot tell from
  locals, parameters and other structs' fields.  They wait for the front end's
  field-use mode; the script skips them under `--scoped`.  The `struct pp`
  sub-struct grouping of `rename-lexpp-groups.md` goes with them.

Names still to decide are as listed in step 5 (`hent.span`, `rst`, `hm`,
`pend`, ...): they are in the table with the review's draft word.  By hand
afterwards: `builtin`'s prototype in `pp-internal.h`, `bench/vcc-diagcov.py`'s
`PP_ERROR`/`PP_WARN`/`PP_FATAL`, the `T_*` lists in comments of
`codegen/walk/proto.h`.  Not renamed (history): `TODO.md` lines that tell what
was done under the old names, and `doc/` other than the plans' own text.
Proof: both builds, `vcc-ctests` 2129/2129, `vcc-diag` 226/226,
`vcc-same.py` 15203/15211 outputs identical (the 8 are `-E` of files that
include vcc's own headers: the absolute path of the checkout is in their line
markers; equal once it is normalised), parity 873/873 and sema 878/879
(modpost.c), `vcc-diagcov.py` unchanged, `gen-diagids --check`.
