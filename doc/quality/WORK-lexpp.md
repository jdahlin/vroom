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

Not cut, with the reason, when a clean cut was impossible: see "Status".

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

(Step 1, written as the split was done: see below.)
