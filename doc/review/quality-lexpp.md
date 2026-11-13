# Quality review: lexer, preprocessor, header cache (2026-11-13, main at 2e6d3e73)

## 1. Summary

The area is fast and well commented where it is hard (the lexer's window loop,
the expansion rules, the header cache's inputs), and the clone scan finds little
(5.7 % in `pp`, 6.3 % in `lex`, most of it the lexer's punctuator handlers).
What costs today is not copied text but structure:

1. **Language modes are done by rewriting a file's bytes and lexing again**
   (four `vlex_*` copies, applied in two places, never cached), and lexing
   knowledge is written again outside the lexer (splices, spelling, paste
   classification, comment scanners, number and character values).  C++ needs
   modes in the lexer itself; today's way already gives wrong output.
2. **Files do not hold what their names say.**  `setup.c` is the `-E` printer
   (half of it), `pp_next`, the region API for the front end, and set-up;
   `ppimpl.h` holds three modules' types and 295 lines of inline code; the
   header cache's three files are cut by history, not by job.
3. **Eleven caches, six lock-free tables, three bump allocators, two buffer
   reuse schemes**, each written by hand with its own full-table policy.  No
   one place lists the caches; the header cache's key and what happens to it at
   a new build are stated nowhere.

Two bugs were found on the way (Q-lexpp-02, Q-lexpp-07).

## 2. Inventory

| File | Lines | What it does | Better name / home | Header comment |
|---|---|---|---|---|
| `include/frontend/lex/lex.h` | 113 | tokens, `vlex`, the token cache's interface | | good; stale (three masks: there are eight; "gnu11") |
| `frontend/lex/lex.c` | 2064 | byte classes (NEON, AVX2, AVX-512, scalar), the token loop, spelling, four mode rewrites, a per-thread arena, file reading, the token cache, the read-ahead threads | `lex.c` (to line 1538) + `lexcache.c` (the rest) | label only |
| `bin/lextest/lextest.c` | 325 | dump, bench, idcheck, core timing | | good |
| `bin/lextest/lexdiff.py`, `lexpmc.sh` | 118 | lexer vs `clang -dump-raw-tokens`; PMU runs | `bench/` (no harness or CI job names them) | good |
| `include/frontend/pp/pp.h` | 377 | options, `pp_next`, `-E`, regions and lazy bodies for `cc`; lines 150-314 are the diagnostics API | diagnostics part to its own header (quality-diag) | good |
| `frontend/pp/ppimpl.h` | 1125 | see Q-lexpp-04 | + `hcimpl.h` | good at the top; six section labels name the wrong file |
| `frontend/pp/setup.c` | 1089 | see Q-lexpp-03 | four files | label, names a third of the content |
| `frontend/pp/token.c` | 278 | `ptok` spelling, context push/pop, `raw()`, `next_exp_slow` | `context.c`, with `pp_next` | label |
| `frontend/pp/text.c` | 32 | `out_reserve` and the spare `-E` buffer only | into the printer file | label; misleads (the `-E` text is in `setup.c`) |
| `frontend/pp/file.c` | 200 | the shared bump allocator (`salloc`), the directive skeleton (`pfile`), the line table | `skeleton.c`; `salloc` to `lib/` | label; stale ("arenas": none here) |
| `frontend/pp/macro.c` | 337 | canonical macros, `define()`, `-dM`/`-dD` text (150 lines) | | label; "the macro table" is in `ppimpl.h` |
| `frontend/pp/directive.c` | 582 | conditionals, `parse_define`, `#pragma`, `#line`, `-imacros`, the dispatcher | `parse_define` to `macro.c` | label |
| `frontend/pp/expand.c` | 1014 | argument collection, pre-expansion, `#`, `##` (with a token classifier), substitution, `__VA_OPT__` | | label |
| `frontend/pp/builtin.c` | 396 | builtin macros, header names, the `__has_*` tables | | label |
| `frontend/pp/ifexpr.c` | 433 | `#if` evaluator; values of pp-numbers and character constants | | label |
| `frontend/pp/include.c` | 1027 | include search and its answer cache, `enter_file`, read-ahead of a new file's includes, `#include`, `#embed` (lines 692-1027) | + `embed.c` | label |
| `frontend/pp/dircache.c`, `.h` | 135 | which directories exist | | good |
| `frontend/pp/hcache.c` | 432 | option signatures, the key table, the `#pragma once` set, the chunk store, validation, text replay (`emit`) | Q-lexpp-09 | label; stale ("types": they are in `ppimpl.h`) |
| `frontend/pp/hcrec.c` | 66 | recording slots, `__COUNTER__` provenance, `mdef` | Q-lexpp-09 | label |
| `frontend/pp/hcentry.c` | 908 | token replay, lazy bodies, `rec_tok`, child attach, apply, try, begin, end, publish (text and tokens), debug output | Q-lexpp-09 | label, names a third |
| `tools/gen-ppdata.sh`, `gen-x86data.sh`, `gen-ppdata-order.py` | 198 | clang's answers into the `.inc` tables | | good |

**What the module is for (draft opening comment).**  `lex` turns a file's
bytes into an immutable array of 16-byte tokens, once per process, and interns
identifiers; nothing above it reads source bytes again except to spell a
token.  `pp` is translation phase 4 as a pull model: `pp_next` returns one
expanded token.  Invariants a newcomer must know:

- A `vlex_file` and everything hung on it (`pfile`, macro definitions of its
  `#define` sites, include answers of its `#include` sites) is immortal and
  shared by all threads; per-TU state is `struct pp` only.
- Source text is readable 128 bytes past its end, a spelling 16 bytes; paths
  handed to `vlex_get` 8 bytes.  Every allocator in the area pads for this.
- "The same macro definition" is pointer equality (`pmacro` is canonical); the
  header cache depends on it.  Bit 0 of a macro table entry means "being
  expanded".
- `tmp` is reset whenever only file contexts are on the stack and the consumer
  holds no tokens (`pp_hold_tmp`); a spelling made by pp does not outlive that.
- A location is file base + byte offset; a token from an object-like macro
  carries the invocation's location, not its own.
- Expansion is clang's algorithm, not the standard's wording: parity with
  `clang -E` is the test.

## 3. Passes and data flow (L4)

Three token formats: `vtok` (16 bytes, lexer), `ptok` (24, `pp_next`'s
result), `htok` (12, header cache); `cc` adds `lztok`.

A file's first use in the process:

| # | Walk | Over | Computes | Where |
|---|---|---|---|---|
| 1 | `read()` | bytes | a padded copy (BOM blanked) | `lex.c:1581` |
| 2 | `classify` | 64-byte blocks, 8 ahead; comments mostly skipped | 8 bit masks | `lex.c:152` |
| 3 | token loop | masks, slow handlers for literals and splices | `vtok[]` | `lex.c:1150` |
| 4 | `intern_list` | identifiers only | ids | `lex.c:964` |
| 5 | `pfile_build` | **all tokens, twice** (count `#`, then build) | directive skeleton, guard | `file.c:87`, `92` |
| 6 | `prefetch_includes` | directives | read-ahead hints | `include.c:436` |
| 7 | `file_line` (first `__LINE__`, diagnostic or `-E`) | **all bytes, twice** | newline offsets | `file.c:177`, `181` |

Walks 5 and 7 recompute what walk 2-3 had: the lexer knows every `#` at a
line start and has the newline mask.  With `-std=c89`, `-trigraphs`, strict
C90 or C23, walks 1-5 run again on a rewritten copy, per TU (Q-lexpp-01).

Per TU, a miss: `vtok` to `ptok` one token at a time (`vfill`); a false group
is one jump.  A function-like macro: `collect_` (arguments in place, or copied
to `sv`), `needs_pre` (each argument scanned for macro names), pre-expansion
into `sv`, substitution into `tmp` (`subst_simple` walks the body twice: size,
then fill; the paste path builds in `sv`, compacts, copies to `tmp`), then the
result is read again as a context.  A token in a nested argument is copied up
to four times; that is clang's model.

Header cache, token mode.  Recording: `rec_tok` copies every delivered `ptok`
(`hcentry.c:129`); at the region's end `publish_tok` converts to `htok`
(one walk), walks the result **twice more** for brace pairs
(`hcentry.c:611`, `615`) and once to hash it (`hc_blob`).  A hit:
`hc_valid` walks the entry's reads and its children's (memoised per lookup),
`apply_delta` the writes; when the hit is inside a region being recorded,
`kid_ex` walks the same read tree again without the memo and `kid_writes` the
same write tree again (`hcentry.c:195`, `218`).  Replay is `htok` to `ptok`
per token; bodies are skipped by a binary search.

`-E` text: `render` per token; for each line-start token `loc_line`
(two binary searches) and `tok_col` (a third, then a backward byte scan).
`-C` scans the bytes between tokens again for comments (`setup.c:715`).

Files holding several passes: `setup.c` (delivery and printing), `hcentry.c`
(record, publish, replay), `lex.c` (lexing and caching).  Passes spread over
files: recording a region (`hcrec.c`, `ppimpl.h` inlines, `hcentry.c`,
`include.c:204`, `builtin.c:273`, `setup.c:930`, `expand.c:360`); the `-E`
printer's state (Q-lexpp-14).

**The caches** (the question for this area).  Each has a comment where it is
defined; no document or header lists them together.

| Cache | Job | Key | Invalidation | Thread safety | Stated? |
|---|---|---|---|---|---|
| token cache `ctab`, `lex.c:1708` | path to bytes and tokens, misses too | the path string as given (two spellings: two entries) | `vlex_cache_flush` (vccd, new build); no mtime check; entries leak | chained CAS insert, state machine, waiters spin | `lex.h:85`; the key is not |
| read-ahead queue, `lex.c:1982` | bytes read before the lexing thread asks | the same entries | none | mutex + condvar ring; 3 detached threads | `lex.c:1976` |
| directory cache, `dircache.c` | does a directory exist | directory path | `vdir_flush` | chained CAS insert | yes |
| `pfile` on `vlex_file.pp` | directive skeleton, guard, line table | the file | with the file | set once by CAS | job only (`ppimpl.h:9`) |
| `pfile.mac[]`, `pfile.inc[]` | a `#define` site's macro; a `#include` site's last answer | directive index (answer checked against sig, mode, directory) | with the file | set once / overwritten | one line each |
| include answers `inc_tab`, `include.c:149` | lookup to file | options sig, path sig, mode, includer's directory, name | `pp_include_flush`; stops inserting at 3/4 full, silently | open addressing, CAS | `ppimpl.h:612`, good |
| canonical macros `canon_tab`, `macro.c:7` | equal definitions are one pointer | content | never; 64 probes, then per-TU | open addressing, CAS | `ppimpl.h:109` |
| header cache `hkey_tab`, `hcache.c:8` | an `#include`'s tokens or text and macro effects | (sig, mode, file pointer, search dir, spelling, system) plus each entry's recorded inputs | never; entries of flushed files become unreachable and stay in the 16 GB region (inferred); cap 8 GB, 64 variants per key | CAS; entries immutable | inputs yes (`ppimpl.h:182`); key and invalidation no |
| chunk store `hck_tab`, `hcache.c:151` | read/write arrays and spellings stored once | content hash | never; 64 probes, then unshared | CAS | yes |
| signature list, `hcache.c:22` | option and search-path strings to ids | content | never; 65 536, then no caching | **one mutex, linear search** | job only |
| per-TU front caches (`hm`, `bcache`, `kspell`, `ckc`, `vkc`) | skip the shared tables | hashes | per TU | none needed | one line each |

## 4. Findings

| Id | Lens | Class | Evidence | What it costs | Proposed home or fix | Size | Risk | Proof |
|---|---|---|---|---|---|---|---|---|
| Q-lexpp-01 | L4, L1 | redundant walk, duplicate | `lex.c:1655`, `1753`, `1814`, `1880`; applied at `include.c:59-70` and `setup.c:124-127` | per TU re-lex in four modes; wrong `-E`/`#x` for `1'000`; blocks C++ | a mode word for `lex_mode` (it has `digraphs` already); cache per mode | M | medium | `vcc-diff.py text`, `lexdiff.py`, fuzz-lex (NEON = scalar), `vbench.py gate`; output changes |
| Q-lexpp-02 | L9 | misplaced (bug) | `include.c:849-882` through `try_path`; `lex.c:1597` | `#embed` data is lexed, BOM becomes spaces, mode rewrites drop bytes | a byte-reading entry in the token cache without lexing | S | low | `vcc-embed-search.py` plus new cases; output changes |
| Q-lexpp-03 | L5 | mixed file | `setup.c`: 1-185 open, 187-283 `pp_next`, 285-451 regions, 453-978 printer, 1046 close | nobody finds `pp_next` or the printer | `open.c`, `context.c` (with `token.c`), `region.c`, `etext.c` (with `text.c`, `macro.c:184-337`) | S | low | `vcc-same.py`, both builds |
| Q-lexpp-04 | L7, L6 | mixed file | `ppimpl.h:180-437`, `439-568`, `629`, `664-828`, `830-1123` | every pp file compiles all of it; labels point to wrong files | `hcimpl.h`; structs to their one file; `struct pp` in sub-structs | M | low | both builds, `vcc-same.py` |
| Q-lexpp-05 | L2 | pattern | tables: `lex.c:1711`, `dircache.c:74`, `macro.c:40`, `include.c:293`, `hcache.c:80`, `179`; allocators: `file.c:10`, `dircache.c:53`, `lex.c:1545` | six tables, four full-table policies; a fix to one misses five | `lib/`: an insert-only concurrent table and a shared bump region | M | medium | `vcc-same.py`, TSan batch, `vccd-cc.py` |
| Q-lexpp-06 | L1 | duplicate | lexing outside the lexer, list below | copies drift (two already differ); each is a place C++ must touch | `lex` exports: spell a `ptok`, lex one token from bytes, skip a comment | M | medium | `vcc-diff.py cmp`, `fuzz-diff`, `vcc-ctests.py` |
| Q-lexpp-07 | L2, L8 | pattern (leak) | list of recording buffers 5 times: `ppimpl.h:652`, `hcentry.c:433`, `439`, `setup.c:1074`, `1078` | the unmap branch of `pp_close` (the thread already holds a spare set) skips `rec.kid` and `rec.kex`, 16 MB of address space each; how often it runs was not checked | the `vmreg` slots the other TU tables use | S | low | `vccd-test.py`; a mapping count test is missing |
| Q-lexpp-08 | L3 | thread | `hcache.c:21-46`, called `ndirs + 2` times from `sig_of` per `pp_open`; `malloc(1 << 17)` at `:51` | one mutex and a linear `memcmp` scan at every TU start | the table of Q-lexpp-05; hash by content | S | low | `vccd-cc.py`; cost unmeasured (quality-scheduling) |
| Q-lexpp-09 | L5 | misnamed | `hcache.c`, `hcentry.c`, `hcrec.c` (inventory) | "entry" and "rec" do not say where validate, publish or replay are | `hc_key.c` (signatures, keys, validation), `hc_store.c` (chunks), `hc_record.c`, `hc_replay.c` | M | low | `vcc-same.py` |
| Q-lexpp-10 | L1 | near-duplicate | plain token read: `token.c:137-157`, `ppimpl.h:1037-1055`, `setup.c:259-270`; context pop: `token.c:201-209` = `219-227`; tmp reset `setup.c:189` = `268` | a new context type or flag is added in four places | one `always_inline` reader; keep the layout the gate likes | S | perf | `vbench.py gate`, `vcc-same.py` |
| Q-lexpp-11 | L4 | redundant walk | `file.c:87`/`92`; `file.c:177`/`181`; `hcentry.c:611`/`615`; `hcentry.c:195`, `218` after `hcache.c:369` | repeated sweeps, section 3; cost not measured | lexer returns the `#` count and newline offsets; braces in the conversion loop; memo in `kid_ex` | M | low | `vperf` before and after, `vcc-same.py` |
| Q-lexpp-12 | L1 | duplicate | reads check `hcache.c:318-329` = `375-389` = `hcentry.c:370-395`; `hexf` `hcache.c:285` = `kexf` `hcentry.c:169`; split loop `hcentry.c:549-606` = `783-807`; `hcache.c:198-205` = `213-220` | the validity rule exists three times | one iterator over an entry's reads with a callback or macro | S | low | `vcc-same.py`, `vccd-test.py` |
| Q-lexpp-13 | L1, L10 | duplicate, dead | punctuators: `lex.c:32`, `98-107`, `523`, `536-553`, `1086`, `1345-1494`, `expand.c:428`; never read: `cls`, `fcls`, `ptab_alt`, `C_*`, `K_*`, `PT_*` (`lex.c:490-504`) | C++ adds `<=>`, `.*`, `->*` in seven places | one X-macro table (kind, spelling, clang's name); delete the dead tables | S | low | `lextest core` hash (`vbench.py`), fuzz-lex |
| Q-lexpp-14 | L4 | mixed | printer fields written in `token.c:208`, `expand.c:795`, `943-965`, `directive.c:394-404`, `429`, `include.c:541`, `590`, `hcentry.c:303-314` | expansion cannot be read without the printer | a `struct etext` in `pp`, set through three or four calls | M | medium | `vcc-diff.py text`, `textc`, `lines` |
| Q-lexpp-15 | L1 | near-duplicate | path joins `include.c:98-106`, `115-121`, `503-507`, `857-883`; search order in `find_include_` and again in `prefetch_includes:484-512` | read-ahead guesses another path when the search changes | one "candidates of a lookup" iterator used by both | S | low | `vcc-diff.py hdr`, `deps` |
| Q-lexpp-16 | L8 | global state | `setup.c:58-113` (`ppr[0..7]`), `text.c:7`, `hcentry.c:421-423`, `lex.c:1543`, `1572` | two reuse schemes; slot numbers are magic; lexer thread-locals never freed | one scheme; named slots | S | low | `vccd-test.py`, TSan |
| Q-lexpp-17 | L8 | global state | 12 knobs (15 with `lextest`'s); `VPP_HC_DEBUG` read three ways (`setup.c:987`, `hcentry.c:404`, `840`); `VCC_TRACK_DIAG` per `pp_open` (`setup.c:95`); lazy flags `include.c:25`, `434`, `hcentry.c:404`, `644` | `getenv` on warm paths; four hand-made "read once" forms | one options record read at start (quality-driver-server) | S | low | `vcc-same.py` |
| Q-lexpp-18 | L6 | misnamed | small ints with meanings, list in section 5 | `c->top = 2` and `out_text > 1` need the comment at the definition | enums | S | low | compile; `vcc-same.py` |
| Q-lexpp-19 | L1 | duplicate | builtin macro names: `ppimpl.h:98`, `setup.c:21`, `builtin.c:153` | a new builtin is three edits; a missed one indexes `q[]` at NULL | one X-macro list | S | low | `vcc-diff.py cmp` |
| Q-lexpp-20 | L9 | error policy | `directive.c:360` (`#line` dropped after 65 536), `:285` (4096 pushes), `token.c:123`, `include.c:301`, `macro.c:81`, `hcache.c:36` | limits that change behaviour or speed without a message or counter | one policy: fatal, counted, or documented fallback | S | low | `vcc-stress.py` |
| Q-lexpp-21 | L10 | stale doc | `lex.h:10`, `lex.c:862`; `ppimpl.h:87`, `183`, `511-514`, `567`, `673-690`, `792`; `pp.h:96`; `directive.c:69-70`; `file.c:1`; `hcache.c:1` | comments that send the reader to the wrong file | fix with the splits | S | none | review |
| Q-lexpp-22 | L1 | duplicate | where a macro was defined: `expand.c:18-41` (scan of every token of every file) and `diag.c:990-1040` (sorted index) | two answers to one question | the index | S | low | `vcc-diag.py` |
| Q-lexpp-23 | L1 | near-duplicate | list below (small ones) | each is a second place to change | helpers | S | low | `vcc-same.py` |
| Q-lexpp-24 | L7 | misplaced | `lex.h:64-65`: `vlex_file.pp`, `.fixdep` | the lexer's header names its users | keep, as `void *user[2]` with the owners documented, or a side table | S | low | builds |

**Q-lexpp-01.**  The four modes: trigraphs, C89 (no `//`), no digraphs,
digit separators.  Each scans the whole file at every entry
(`memmem`/`memchr`), and on a hit makes a copy, lexes it and returns a new
`vlex_file` that no cache holds.  The header cache keys on the file pointer
and `pfile` hangs on it, so such a file is lexed and skeletonised again for
every TU and can never hit.  `vlex_digitsep` removes the `'`: checked with
`build/src/vpp -E -P -std=c23`, `#define S(x) #x` / `S(1'000)` gives `"1000"`
and `int b = 1000;` where clang gives `"1'000"` and `1'000` (TODO.md:792 knows
this one).  Its scanner (`lx_skip`, `lex.c:1792`) knows no splices and no raw
strings.  C++14 has digit separators always, so every C++ file would take this
path.

**Q-lexpp-02.**  Checked with the same binary: `#embed` of a file starting
with EF BB BF gives `32, 32, 32`; with `-std=c23`, a file holding `1'0` gives
`49, 48` (clang: `49, 39, 48`).  The data file is also tokenised for nothing
and limited to 2 GB.

**Q-lexpp-04.**  `ppimpl.h` by part: 122 lines of core types; 258 lines of
header-cache types; `struct pp`, 130 lines and about 150 fields (macro table,
contexts, about 25 diagnostics fields, about 30 printer fields, the recording
state); 165 lines of prototypes; 295 lines of inline functions.  Belongs to
one file: `struct incent` and its comment (`:612-638`, only `include.c` reads
the fields); `dollar_split` (`:1074`, a cold non-inline function defined in a
header: `token.c`); `touch_write_kid` (`:920`, only `hcentry.c`);
`rec_spare_t` (gone with Q-lexpp-07); `hstat_t`, `hck`, `hvec`, `HV_FOR`,
`hlook`, `hseg`, `hkid`, the `HC_MAX*` limits (header cache only).  Section
labels that name the wrong file: "diag.c" lists `move_to_line`, `out_nl`,
`cmt_*`, `tok_col`, `text_file_change`, `lm_*` (all in `setup.c`); "file.c"
lists `hsalloc`, `hc_vec`, `hc_blob` (`hcache.c`); "ifexpr.c" lists `embed`,
`has_embed` (`include.c`).

**Q-lexpp-05.**  The two chained tables are the same code (`dircache.c:32`
says so).  The four open-addressing ones differ only in what happens when
full: stop inserting at 3/4 (`inc_tab`), give up after 64 probes
(`canon_tab`, `hck_tab`), scan the whole table (`hkey_tab`).  `salloc` and
`dalloc` are the same function.  Set-once slots by CAS, loser wastes its
copy: `file.c:18`, `162`, `185`, `macro.c:142`, `dircache.c:63`.  One-time
init is six `pthread_once` sites plus a hand-made one: `lex.c:1082-1117`
tests `disp[0]` atomically, takes a mutex and calls `pthread_once` inside it.
`common/intern.c` has the seventh table (quality-lib).

**Q-lexpp-06.**  Lexing knowledge outside `lex_mode`:
- `token.c:17-30` `splice_at` = `lex.c:603-615` `splice_len`; they differ on
  `"\n\r"`.  `token.c:32-49` `pp_spell` = `lex.c:1515-1538` `vlex_spell`; only
  the second keeps a raw string's body (`TF_RAW`).  Not run.
- `expand.c:471-524` `classify`: a second lexer for `##` results (identifier,
  number, punctuator, prefixed literal) with its own tables `puncts[]` and
  `idch_tab` (= `lex.c` `isid`) and `ucn_at` (= `lex.c:676` `ext_id`).
- comment and literal scanners over raw bytes: `lex.c:1792` `lx_skip`,
  `setup.c:655` `cmt_line_end`, `setup.c:715` `cmt_flush`.
- `ifexpr.c:23-37` = `frontend/c/lit.c:156-170` (digits of an integer) and
  `ifexpr.c:79-88` = `frontend/c/lit.c:291-300` (escapes); `ppimpl.h:282-288`
  `htok_get` = `frontend/c/token.c:100-106`.
- UTF-8 and UCN: `setup.c:377-412`, `lex.c:664-705`, `frontend/c/lit.c`.

**Q-lexpp-10.**  These are deliberate (the comments give the reasons:
no frame, tail calls).  The proposal keeps the generated code and removes the
second text: `pp_next`'s body is `raw_plain` plus the tmp reset.  The same
holds for the four comma/paren scans in `collect_` (`expand.c:81-103`,
`123-136`, `144-163`, `176-193`).

**Q-lexpp-23.**  Small second copies: new uncached `vlex_file`
(`lex.c:1657`, `1777`, `1868`, `1912`); a memory buffer as a `vlex_file`
(`setup.c:62-80`, `169-180`); `tok_col`'s search (`setup.c:808-822`) =
`loc_fent` (`diag.c:11-23`); the empty-expansion line (`expand.c:795`, `899`,
`973`, `1003`); `P_DIRECTIVE` tokens (`directive.c:326`, `505`, `555`);
`extra_loc` = `dir_extra_tok` (`directive.c:7`, `16`); file not found
(`include.c:678`, `951`); `has_query`'s two searches (`builtin.c:174`, `190`);
`write_escaped` (`setup.c:470`, `lextest.c:43`); `lextest.c`: `now`, `worker`,
FNV (`:34`, `:131`, `:207`); FNV again in `pp_files` (`setup.c:1025`); seven
hash mixes in the area (CRC32C twice, FNV twice, three multiply-xor forms).
`parse_define` returns its error as a string and `macro.c:126` picks the
message by `err[0]`.

**What is C-specific (the C++ question).**
- Lexer: no `<=>`, `.*`, `->*` kinds (`lex.h:23-40`; `::` exists); `u8'a'`
  is not a token (`lex.c:892`, `sq` 0; `ifexpr.c:237-244` repairs it for C23
  in `#if` only); raw strings are lexed in every mode (`lex.c:893-896`); no
  literal suffixes (`"x"_s`); no `<::` rule; digit separators by rewrite;
  identifier characters are C11 annex D (`lex.c:638`); `$` in identifiers is
  split back by pp for assembler (`ppimpl.h:1074`).
- Token cache: keyed by path only.  A mode-dependent lexer needs the mode in
  the key, or one superset token stream with fix-ups in pp (both exist today:
  `dollar_split` is the second way).  Decision 1.
- pp: `true` is 1 in `#if` only with `c23` (`ifexpr.c:231`); C++ has `true`,
  `false` and the alternative tokens (`and`, `not`, ...) as operators, and
  they may not be macro names; `__has_cpp_attribute` is not registered
  (`setup.c:28`); `__is_identifier` answers 1 always (`builtin.c:381`); the
  `__has_*` answers and predefined macros were taken from clang as C
  (`gen-ppdata.sh:20-23`, `-std=gnu11`): a language column is needed beside
  the target column; `pp_opts` has `c89`, `c23`, `trigraphs`, `no_digraphs` as
  separate ints, no language field; `sig_of` (`hcache.c:57`) must take the
  language or C and C++ share header-cache entries.
- Keywords are not pp's concern: `include/common/intern.h:17` lists C's with fixed ids
  and `frontend/c/token.c:4-50` classifies them per standard.

## 5. Names (L5, L6)

| Name | Where | What it is | Proposal |
|---|---|---|---|
| `rg`, `ru`, `rv`, `dl` | `hent`, `ppimpl.h:342-345` | inputs: guard tests; ids read while undefined; other reads with their value; the entry's own writes (id, new value) | `guards`, `undef_reads`, `value_reads`, `writes` |
| `tk`, `xtk` | `hent:350` | own tokens as `htok`; the full `ptok`s of those that do not fit ("escapes") | `toks`, `wide_toks` |
| `ord`, `oadd` | `hent:346` | `#pragma once` membership it tested; files it marked once | `once_tests`, `once_added` |
| `ctr`, `crd`, `seg`, `kid`, `lk`, `br` | `hent` | `__COUNTER__` digit positions; lookups of names pasted from the counter; where children splice in; child entries; include lookups; brace pairs | spell out |
| `pk`, `pk_L`, `pk_pp`, `pk_end` | `pp`, `hent` | the printer's previous token: kind, was the identifier `L`, a `.` came before it, where it ended | `prev_kind`, ... in `struct etext` |
| `sv`, `svn` | `pp:446` | the scratch token stack of expansion | `scratch` |
| `cx`, `fe`, `tev`, `drh`, `drs`, `dn`, `dtr`, `mdx`, `wc` | `pp` | context stack; file entries; top-level region events; hit regions and open hits for declaration replay; `#pragma diagnostic` nodes and transitions; `#define` index; cached warning states | spell out |
| `t0 o0 c0 r0 s0 k0 x0 l0` | `hlev:420-427` | a level's start in eight recording stacks | `start.touch`, ... |
| `lm` / `lmark`, `lm_*` | `pp:498`, `:519`, `setup.c:461-556` | the `#line` table, and the `-E` line-marker printer | `line_tab` and `marker_*` |
| `emit` | `hcache.c:412` | prints a cached entry's text into the `-E` buffer | `hc_print_text` |
| `vm`, `salloc`, `hsalloc`, `dalloc`, `arena` | `setup.c:7`, `file.c:10`, `hcache.c:144`, `dircache.c:53`, `lex.c:1545` | reserve address space; shared bump; the same, counted for the cache; the same again; per-thread bump | one family in `lib/` |
| `vdir_exists` | `dircache.h` | returns 0 for "cannot exist", -1 for "ask open()" | `vdir_missing`, bool |
| `hc_top` | `hcache.c:278` | may an `#include` here be a region | `hc_region_ok` |
| `out->id = 1` | `directive.c:578`, read `setup.c:856` | a `P_DIRECTIVE` token that is an assembler line | a flag |

Small ints with meanings (Q-lexpp-18): `tokmode` 0/1/2, `out_text` 0/1/2 (2 is
`-C`), `ctx.top` 0/1/2, `pfile.bad` 1/2, `rec_read`'s `def` 0/1/2, `expand()`
returns 0/1/2, `vlex_no_intern` 0/1/2 (2: classify only), `markers` and
`lm_sys` 2, `sdir` -1/-2 (-2: framework), `vlex_stat[310..321]`.

Prefix conventions.  Public: `pp_`, `vlex_`, `vtok_`, `vdir_`.  Internal:
mostly none (`define`, `include`, `embed`, `expand`, `builtin`, `directive`,
`raw`, `push`, `emit`, `publish`, `load`, `claim`, `classify`, types `ctx`,
`fent`, `ev`); these are global symbols in the file-by-file build.  Topic
prefixes exist and mostly hold: `hc_` (but `replay_*`, `rec_*`, `kid_*`,
`touch*`, `sig_of`, `hkey_get`, `emit`, `publish` are header cache too),
`lm_`, `cmt_`, `dd_`/`dm_`, `emb_`, `inc_`, `m` for the macro table (`mget`,
`mdef`, `mguard`).  `D_*` is directive kinds in `ppimpl.h` and directory
states in `dircache.c`; `C_*` is context types in `ppimpl.h` and (dead) byte
classes in `lex.c`.

Words with two meanings: `ctr` (a token's provenance index, an entry's digit
positions, a region's counter value, with `counter` the value itself); `fe`
(the array, an index into it, an entry's file list, a byte count in
`hc_account`); `rec` (recording state, "recorded" counter; `frontend/c/replay.c` uses
it for declaration records); `replay` (header-cache token replay here,
declaration replay in `cc`); `top` (outermost replay context, top-level
region, `hc_top`); `classify` (`lex.c:152` bytes, `expand.c:471` a pasted
token); `stringify` (`expand.c:346` is `#x`, `lextest.c:43` is escaping).

## 6. Decisions needed

1. **How a language mode reaches the tokens.**  (a) A mode word in the lexer
   and in the token cache's key: simple, but C and C++ TUs of one vccd lex
   shared headers twice.  (b) One superset token stream per file and cheap
   fix-ups in pp (as `$` today).  (c) Today's rewrites.  Recommendation: (a)
   for what changes token boundaries (digraphs, `//`, trigraphs, digit
   separators, raw strings, `u8'a'`, C++ punctuators), with the modes
   collapsed to the few classes that differ; measure the twice-lexed share in
   a mixed build before choosing (b) for any of them.
2. **Hot-path copies** (Q-lexpp-10): keep the generated code and unify the
   source, or leave them.  Recommendation: unify behind `always_inline`,
   accept only at 0 % on `vbench.py gate`.
3. **One concurrent table and one shared region in `lib/`** (Q-lexpp-05),
   with one full-table policy.  Recommendation: yes; chained, insert-only,
   never full; needs quality-lib's agreement and a probe-count check on
   PostgreSQL (`ppimpl.h:623` records a past problem there).
4. **The header cache at a new build.**  Entries of flushed files stay in the
   region until vccd restarts.  Options: document it; or reset the region and
   the tables when no TU runs.  Recommendation: document now, decide with the
   scheduler work.
5. **The `-E` printer**: stays in `pp` behind a narrow state struct
   (recommended), or moves to `driver/` on a public event interface.
6. **`pp.h`**: split the diagnostics API out (with quality-diag).
7. **Fixing Q-lexpp-02 and the digit-separator output** changes output: they
   are bug fixes outside the byte-identical rule and need their own tests.

## 7. Not covered

- `frontend/diag/diag.c`, `diagout.c`, `diagtext.c` and the generated `.inc` tables (not
  this area); only `diag.c:1-30` and `:990-1040` were read, as the other side
  of duplicates.  `lib/intern.[ch]` and `include/common/arena.h` were read in part.
- `doc/RESEARCH-preprocessor.md`: section 0 and the headings only.
- No instruction counts: the cost of the walks in Q-lexpp-11 and of
  `sig_intern` is unmeasured.  Nothing was built; two checks ran the existing
  `build/src/vpp` (13 Nov) on scratch files (Q-lexpp-01, Q-lexpp-02).
- Not run: the `splice_at`/`pp_spell` differences in Q-lexpp-06; whether
  strict C modes should lex raw strings; whether the file-by-file build links
  with pp's unprefixed names next to other modules'.
- The x86 SIMD paths of `lex.c` (270-385) were read but cannot be judged
  against the NEON path on this host.
- The read-ahead threads and the spin wait in `vlex_get` are left to
  quality-scheduling; the gen scripts were skimmed, not reviewed.
