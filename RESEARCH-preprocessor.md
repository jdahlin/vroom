# Preprocessor: state of the art and design for vcc (step 2.3 / 2.4)

Research of 2026-11-07. Sources: three research passes (clang/GCC/tcc/warp code, papers,
LPC talks) and local measurements on the arm64 kernel build (`work/build-o0` flags, Homebrew
clang 23). Points the research could only infer, not verify, are marked *(unverified)*.

## 1. The workload (measured here)

| per TU (clang `-E -Xclang -print-stats`) | `fs/ext4/inode.c` | `kernel/sched/fair.c` | `lib/klist.c` |
|---|---|---|---|
| `#define` executed | 30 988 | 31 055 | 23 436 |
| `#if/#ifdef/#ifndef` (regions skipped) | 3 929 (2 307) | 4 310 (2 571) | 1 935 (1 182) |
| `#include` (skipped by include guard) | 2 496 (1 719) | 2 819 (1 939) | 1 030 (630) |
| macro expansions object / function-like | 60 953 / 33 847 | 62 965 / 37 960 | 23 515 / 13 089 |
| identifiers in the table | 72 468 | 76 023 | 45 630 |

Whole build (`clang -M` for all TUs, normal kbuild flags):

- 1 166 TUs read **5.63 GB** of source. There are only 3 362 distinct files (46 MB), so
  every byte is preprocessed **121 times**. Median TU: 565 files, 5.4 MB.
- Roughly **35 M `#define` executions** and **~100 M macro expansions** per build, nearly
  all of them repeats of identical work.
- Time, in clang, on 13 sampled TUs:
  - `-E`: 242 ms mean
  - `-fsyntax-only`: 299 ms
  - `-c -O2`: 394 ms

  So **preprocessing is 61 % of an -O2 compile** and the front end is 76 %. About 280 s
  of CPU per build goes to the preprocessor alone.

Conclusion: the dominant cost is *repetition across TUs*, then per-expansion cost. Raw
lexing is already solved (token cache, each file lexed once).

## 2. Prior art

### Macro expansion
- **clang** (TokenLexer, `HandleMacroExpandedIdentifier`, MacroArgs):
  - **No hide-sets.** A macro is disabled while its TokenLexer is on the stack, and is
    re-enabled when the lexer is popped. An identifier met while its macro is disabled
    gets a permanent `DisableExpand` paint bit (C99 6.10.3.4p2). The state is one bit per
    macro and one bit per token.
  - Fast paths: a 0-token object-like macro is never pushed; a "trivial" 1-token macro is
    replaced in place.
  - Argument pre-expansion is lazy and cached per argument, but it re-enters `PP.Lex`.
  - TokenLexer and MacroArgs objects are recycled through free lists.
  - Subtle point to match: an exhausted TokenLexer is popped (and its macro re-enabled)
    *before* looking ahead for `(`, in `peekNextPPToken`.
- **GCC cpplib**: a context stack. Object-like bodies are referenced in place; function-like
  results are arrays of token pointers. Arguments are expanded lazily. Painting a token
  copies it. Virtual-location tracking (`-ftrack-macro-expansion=2`, the default) costs
  up to ~13 % memory, and up to ~3× on some code (PR 56746).
- **warp** (Bright, 2014):
  - works on text with in-band escape bytes
  - an "in use" flag per macro, and each argument expanded once into a cached buffer
  - fixed stack buffers and no GC
  - no published throughput number; reportedly not faster than `clang -E`
- **tcc**: flat `int*` token strings and a paint bit inside the token int, but a new
  TokenString allocation per expansion.
- **Plan 9/lcc cpp**: true hide-sets, interned as sorted sets, with an int id per token.
  This is the most compact true hide-set representation.
- **chibicc** follows Prosser literally (string lists, strcmp). Use it as a correctness
  reference only.
- Micro-measurement (Apple clang 21 vs tcc):
  - plain identifiers: ~25 ns/token
  - object-like expansion: ~35 ns
  - function-like expansion: **~0.5 µs** (tcc slightly faster)

  Function-like expansion is the expensive part.

### Avoiding repeated work
- **Include guards** (clang MultipleIncludeOpt, GCC `mi_*`):
  - The controlling macro is detected once per file. Later includes are skipped before the
    file is even entered; 70 % of `#include`s in a kernel TU are skipped this way.
  - Exact rules: no tokens outside the guard, and no top-level `#else`/`#elif`.
- **`#pragma once`**: clang compares by inode; the kernel uses none.
- **Skipping excluded blocks**: clang's `SkipExcludedConditionalBlock` scans raw for `#`.
  Its `DependencyDirectivesScanner` (2022) reduces each file once to a list of
  directives, so skipping costs O(directives).
- **PCH and modules.**
  - GCC's `cpp_valid_state` records the macro names a PCH mentioned and rejects the PCH if
    one is now defined differently. That is an early "read set" validity predicate.
  - clang's PCH only checks command-line `-D`s.
  - Modules isolate macros, which does not fit the kernel (`CONFIG_*` everywhere).
- **Bothner's GCC compile server (2003)**: reuses header "fragments" delimited by
  conditionals, with dependency timestamps and per-identifier "used as non-macro" bits as
  negative dependencies. Measured ≥3×.
- **Tromey's incremental GCC (2008)**: hunk checksums. About ½ the time and ⅓ the memory
  of `--combine`.
- **zapcc** keeps headers resident in a server process, and reports **no gain for C**
  because its wins come from C++ templates.
- **Kernel header work**: Molnar's fast-headers series took builds from 231 s to 130 s;
  Google's LPC'23 work cut `lib/string.o` from 0.36 s to 0.12 s. Build time scales with
  the tokens that reach the front end.
- **Snapshotting macro tables.** HAMT/CHAMP give O(1) snapshots but ~3–4 dependent misses
  per lookup. A flat table plus a version stamp or undo log gives ~1 miss per lookup.

### Parallelism, memory, locking
- **No compiler parallelizes the preprocessing of one TU**, and none pipelines PP → parser
  on separate threads with a measured gain. Cuik, TCC, clang, and zapcc (across processes)
  all parallelize per TU.
- **No compiler shares PP state between threads in one process.** clang's ASTContext is not
  thread-safe, TCC takes a global lock, and Cuik keeps everything per TU. Shared immutable
  header states would be **new design**.
- **clang's hottest spots on the kernel** (LPC 2020 perf profile):

  | Symbol | Share |
  |---|---|
  | `SourceManager::getFileIDLocal` | 3.0 % |
  | `StringMap` lookup (string hashing) | 2.3 % |
  | `Lexer::LexTokenInternal` | 1.5 % |
  | `TokenLexer::Lex` | 1.5 % |
  | malloc/free | ~2.7 % |
  | `ExpandFunctionArguments` | 0.7 % |

  Macro-expansion SourceLocations more than double clang's location space and are the
  reason for the 2021 "out of SourceLocation space" RFC. All of these go away with
  interned ids, arenas and lazy locations.
- **aro** (Zig's C front end) keeps tokens in SoA form, with expansion locations in a
  *sparse side table* that only expanded tokens get.
- **M1 cache line = 128 bytes** (`hw.cachelinesize`), so per-thread hot data must be
  padded to 128, not 64.

## 3. Design for vcc

### 3.1 Expansion engine (2.3)
1. **clang's disabling model, not Prosser hide-sets.** Use a "disabled" bit per macro in
   the per-TU macro table and a paint bit in `vtok.flags`. This gives exact clang parity
   (the target of the 2.3 acceptance test) and needs no allocation. *(This changes plan §3,
   which said Prosser.)*
2. **Context stack without copying.** A context is (pointer, length) into immutable tokens:
   - object-like macro bodies point straight into the defining file's token array;
   - function-like macro results are emitted into a per-thread arena that is reset per TU.
   Contexts live in a fixed array (depth is bounded), with no malloc.
3. **Fast paths:** 0-token and trivial 1-token object-like macros push no context.
4. **Macro definition = precomputed template**, built once per `#define` and immutable:
   - the body slice
   - parameter count and variadic kind
   - per-body-token opcodes (literal / param / `#param` / `##` / `__VA_OPT__`)
   - a "needs pre-expansion" bit per parameter
   - a has-paste flag

   Expanding then becomes a loop over opcodes.
5. **Arguments** are ranges into the caller's stream. Pre-expansion is lazy, cached once
   per argument, and runs in a direct loop rather than a re-entrant `lex()`.
6. **`(` lookahead** walks finished contexts the way `peekNextPPToken` does, popping them
   first, which matches clang's re-enable timing.
7. **Locations: lazy and sparse.** A file token knows (file, offset). An expansion gets one
   record (macro, call site), and a token produced by an expansion carries only that
   record's id. Line numbers are computed only for `__LINE__`, diagnostics and debug info,
   from a per-file newline index built on demand.
8. **Pull model:** the parser pulls tokens one at a time, so the working set stays at the
   context stack plus current files. There is no 11 MB-per-TU output array, except where
   the header cache (§3.3) decides to keep a region.

### 3.2 Per-file and per-TU data (2.3)
- **Directive skeleton per file, built once and shared** (in the style of clang's
  DependencyDirectivesScanner): a list of the file's directives with their kind and the
  indexes of the matching `#else`/`#elif`/`#endif`. A false `#if` block becomes a jump.
  The skeleton is built in the lexer pass or lazily on first use, and stored in the token
  cache entry.
- **Include guard detected once per file** and stored as `controlling_macro` in the cache
  entry. On each `#include`: if the guard is defined, skip the file without touching it.
- **Include resolution with no syscalls.** `stat()` per search directory per `#include` is
  ~2.5 k includes × N directories. Cache directory listings once per directory
  (`getattrlistbulk`) in a lock-free table shared by all TUs, then cache
  (search-path id, name) → file. File contents come from the token cache, read ahead by
  the I/O threads as soon as an `#include` target is known.
- **Per-TU macro table indexed by identifier id.** Ids are dense u32s (~700 k distinct
  across the tree), so use a two-level array:
  - a page directory, then 16 KB pages of `u32 def_index`
  - a parallel bitmap "has macro" / "disabled"

  A lookup is two loads and no hashing. Definitions are immutable records in arenas;
  `#undef` stores 0.
- **Copy-on-write pages for forking** a TU from a shared snapshot (§3.3): a TU copies only
  the pages it writes. This replaces plan §3's "persistent hash tables, COW per bucket":
  the same idea, but without hashing or HAMT depth.
- **All of it in per-thread arenas.** No malloc in the expansion path.

### 3.3 Header cache (2.4), designed into 2.3 from the start
1. **Prefix snapshot.** Every TU starts with the same `-include compiler-version.h kconfig.h
   compiler_types.h`.
   - The macro state after that is frozen once per flags signature.
   - TUs fork it copy-on-write.
   - Validity: the per-TU `-D`s (`KBUILD_MODNAME`, `KBUILD_BASENAME`) are defined before
     the prefix, so they are part of the prefix's *read set*; if they aren't read, they
     aren't keys.
2. **Memoize each top-level `#include` on its read set** (Bothner / GCC `cpp_valid_state`,
   made exact). During expansion of an included region, record:
   - every macro whose definition or definedness was *read*, with its value or "undefined"
     (via the id-indexed table, one bit per id per region recorder);
   - the define/undef delta;
   - `__has_include*` results and resolved paths;
   - the `__COUNTER__` delta;
   - pragma state (`push_macro` occurs in kernel TUs).

   A region that uses `__FILE__`, `__LINE__`, `__INCLUDE_LEVEL__` or `__BASE_FILE__`
   either records them as inputs or is not cached. Cached result: an immutable token
   array (the expanded output), the macro delta, and later the parser's declarations for
   that region (plan §4).
3. **Prefix tree** as in plan §4: the nodes are these memoized regions, chained.
4. **Sharing:** a node is built once and published with release/acquire into a slot table,
   and never freed during the build. No refcounts, no RCU; 128-byte padding on per-thread
   counters.

### 3.4 Parallelism
- **The unit is the TU**: one per P-core from a work-stealing pool, longest first, with the
  I/O threads reading ahead. No parallelism inside a TU and no PP → parser thread split;
  there is no evidence either pays.
- **Cross-TU sharing is where the parallel gain is:** the first TU to reach a prefix-tree
  node builds it, and later TUs reuse it. To avoid all eight cores waiting on the same
  first node, schedule one "pioneer" TU per flags group first.

## 4. Correctness checklist for clang parity (2.3 acceptance)
- Disabling and repaint timing: the pop-before-`(` rule, nested function-like names, and
  `f(2)(9)`.
- GNU `, ##__VA_ARGS__` comma elision, named variadics (`args...`), and `__VA_OPT__`
  (C2x, but clang accepts it in gnu11).
- Empty arguments, and the `IS_ENABLED` / `__take_second_arg` placeholder trick.
- `defined` produced by a macro inside `#if` (clang allows it, with a warning).
- `__has_include` / `_next`, `__has_attribute`, `__has_builtin`, `__has_feature`;
  `_Pragma`; `#pragma push_macro` / `pop_macro`; `#include_next`; `__COUNTER__`;
  `#line`; `__FILE__` with `-fmacro-prefix-map`.
- `.S` files and linker scripts, in assembler-with-cpp mode (no `'` char literals,
  `#` comments).
- The acceptance test compares token streams (kind, spelling, and later locations),
  not text, so clang's avoid-paste spacing only matters for a `-E` printer.

## 5. Order of work
1. Directive skeleton plus include-guard detection in the token cache (cheap, shared).
2. Include resolution with a directory cache.
3. Macro table (id-indexed, COW pages) and the expansion engine; `vcc -E` token-stream diff
   against clang over all 1 166 TUs (`bench/vcc-diff.py`).
4. Read-set recording per included region (measure hit rates before caching anything).
5. Prefix snapshot, then memoized regions (2.4).

## 6. Header cache review (2026-11-07, after the first 2.4 implementation)

### 6.1 Measurements of the committed cache (8b96810)
Setup: all 912 TUs in one process, `vpp --batch`, `-O3`; the machine was shared, with
load 5–7.

| | wall (`-j 6`) | CPU | `-j 1` |
|---|---|---|---|
| no cache (`VPP_NOHC=1`) | 3.3–4.7 s | 20–28 s | ~22 s |
| cache | 0.77 s | 4.6 s | 3.5 s |

- **Hits.** 41.7k region hits against 13.5k misses (76 %), and 2.8 of 2.9 GB of
  `-E` text is replayed. A side session measured a 48 % hit rate, 4.4 MB stored per TU,
  "arena full" and an `-O3` crash, but that was on a snapshot from before nested
  regions, before the fixes for the arena-order and interning crashes, and before the
  prefix-based dir signatures. The committed tree runs clean at `-O3`.
- **Recording cost on a cold TU:** +15–20 % (fork.c 86 vs 73 ms, namei 73 vs 64, async
  44 vs 38). It pays back fast: the first 100 TUs take 0.80 s against 1.94 s without the
  cache.
- **Where the misses come from:** 9.4k of the ~10k misses on existing keys are
  include-guard variants. The same header is reached with a child guard
  defined in one TU and undefined in another.
  - Each miss processes only the header's own text, since the children hit, but misses
    are still 2/3 of the CPU.
  - Tolerating "child already included" (leave the child out on replay) recovered only
    376 hits.
  - The dominant direction is the opposite: the entry was recorded *after* the child
    was included, and this TU has not included it yet. Fixing that needs reads ordered
    relative to the child's position.
- **Token volume:** about 5.2 bytes of `-E` text per token, so a full build replays
  ~550 M tokens (fork.c alone: 0.97 M). Copying 24-byte `ptok`s would move ~13 GB, and
  even at 1–2 ns per token, delivery through `pp_next` costs 0.5–1 s of CPU. The parser
  then still has to parse every one of those tokens.

### 6.2 Prior art (not covered in §2)
- **KDevelop C++ support (D. Nolden, ~2008).**
  - Each header is parsed into an "environment file" keyed on the macros it *used*
    (with values) and *defined*.
  - A header can have several stored versions, and a lookup picks the one whose used
    macros match the current environment.
  - To keep recording cheap, the identifiers ("strings") occurring in the file serve
    as a static superset of what it can depend on.

  This is nested memoization on read sets, as vcc's regions are. KDevelop accepts
  approximations, which we cannot.
- **Eclipse CDT indexer, "significant macros".**
  - A header's significant macros are those tested in `#if`/`#ifdef`/`defined`, plus
    those used before being defined.
  - Versions of a header are keyed on their values, and the index stores several
    variants per header.
  - CDT also reuses any indexed version of a header ("skip already indexed"), which is
    unsound and only good enough for an IDE.
- **EDG automatic PCH, Borland automatic precompiled headers.**
  - The header stop point is chosen automatically (the first non-preprocessor token,
    or `#pragma hdrstop`).
  - A PCH is valid when the prefix of the TU (the text of the directives) and the
    command-line state are identical. The whole prefix is validated at once, with no
    read sets.
- **clangd / libclang preamble.**
  - The preamble is the leading run of directives and comments.
  - It is reused when the preamble text, the compile command and the contents of every
    included file are unchanged. Each edit validates it once.
- **ccache direct mode.**
  - A manifest maps hash(source, args) to a list of (included files, content hashes)
    and the result.
  - It does not record failed lookups, so a header created earlier in the search path
    later (shadowing) is a documented hazard.
  - vcc records how many leading search directories each lookup depended on (`ndirs`,
    `psig`) inside one process. A long-lived vccd must also notice new files there,
    for example with a per-directory generation counter from dircache.
- **Bothner's GCC compile server (2003), re-read.**
  - Its fragments are delimited by conditionals, not files, and its dependencies are
    per-identifier bits ("used as non-macro").
  - Recording overhead was the main cost he reported; the server won because most
    fragments were reused.
  - It cached declarations, not tokens: the parser skipped reused fragments entirely.

### 6.3 Answers
- **Granularity.**
  - Already nested: every `#include` at any depth is its own region with its own read
    set, and a parent's read set is the union of its children's that were not
    satisfied by its own writes.
  - Read sets are big (17 M undef reads stored), but validating them is cheap: about
    10 % of samples, including the copy.
  - The cost is in the variants, not in coarse regions.
- **Whole-prefix variant per flag signature.**
  - It cannot add hits over the current cache. When two TUs share a flag signature and
    a leading include sequence, their states before each of those includes are
    identical, so every one of those region lookups already validates against the
    first TU's entry.
  - A prefix tree only saves the validation (about 10 %), and it fails at the first
    differing include, where region lookup does not.
  - **Not worth building.**
- **Representation.** Text replay serves `-E` only, and in a kernel build `-E` jobs are
  a handful of linker scripts and DT files. The C compile goes through `pp_next` into
  `cc_run`, so the cache has to feed the parser.
  - Token replay with references to shared, immutable token data (the file token
    caches and the immutable macro bodies) is the necessary first step. It still costs
    the parser ~550 M tokens.
  - The goal is Bothner's and the plan's §4: replay the region's *declarations*
    (symbols, tags, types, enumerator values, typedef-ness). The parser then never
    sees the tokens, and a hit costs O(declarations).
  - The read set gains name lookups, because typedef names change the parse. That
    follows the same exact-read-set scheme: lookups of ordinary and tag names,
    recorded per region.
- **Cheap recording.**
  - The recording itself costs 15–20 % on a cold TU.
  - The real cost of "pioneer" TUs is the misses, because guard variants keep
    re-recording a header's own text.
  - Fix the misses by recording, for each read, its position relative to the children
    (its first-touch index is already ordered). Then an entry recorded with child X
    already included can be replayed with X spliced in: validate the reads before X's
    position against the state at entry, look X up at that point, and validate the
    reads after X against the state that results.

### 6.4 Design going forward
1. Keep the region model: nested memoization on exact read sets, lock-free publication,
   and nothing freed.
2. Make the payload per consumer.
   - **Text** stays for `-E`, and is done.
   - **Tokens** come next, for `pp_next`. An entry's own output becomes a rope of runs:
     - (file token array, range) for tokens that pass through unexpanded, which is
       most of a header's declarations;
     - immutable `ptok` arrays in shared memory for expansion results;
     - child references, as for text.

     Locations are a per-entry base plus an offset, as `span`/`rbase` are now.
3. **Declarations** follow once 2.6 gives the front end a stable state layout. They are
   the large win for the compile path. The first form, prefix snapshots, is done (§6.7).
4. ~~**Ordered reads** with child splicing at replay, against the guard-variant misses.~~
   Tried and shelved, see §6.6.

### 6.5 Token payload, measured (step 2 done)
- **Representation.** Own tokens are copied `ptok`s in shared memory:
  - locations are offsets from the region base;
  - spellings that lived in per-TU arenas are copied;
  - `__COUNTER__` digits are kept as (token, byte offset, length).

  A hit pushes a `C_REPLAY` context, and `pp_next` hands its tokens out directly
  (painted, so nothing re-expands). Children are spliced in by segment, counter digits
  are rewritten, and identifiers re-interned with `vintern_copy`, since `vintern` keeps
  the caller's pointer.
- **Exactness.** A per-TU hash of every token's kind, SOL/SPACE flags, location,
  spelling and id, plus the file list, is identical with and without the cache for all
  911 TUs. The sema and layout runs stay green.
- **Speed** (`-j 6`, load 8–11):

  | | with cache | without |
  |---|---|---|
  | token stream | 1.74 s / 10.4 s CPU | 3.1 s / 18.5 s |
  | `-fsyntax-only` | 3.77 s / 22.6 s CPU | 4.80 s / 28.7 s |

  Replay costs ~7–8 ns per token for 541 M tokens and is bound by memory bandwidth.
- **What follows.** Shrinking tokens (16 bytes, or runs into the files' `vtok` arrays)
  would at best halve the token-stream cost. Parsing the replayed tokens now dominates,
  which confirms that declaration replay (step 3) is the large win for the compile
  path.

### 6.6 Ordered reads with child splicing: tried, shelved (2026-11-08)
- **What was built.**
  - Every read and write in an entry carries its phase: the number of skip sites before it.
    A skip site is an `#include` skipped because its guard was defined.
  - On a miss, a variant could be made exact by splicing a child entry in at each site
    whose guard is now undefined. The child had to be valid in the state at that site:
    the TU state at entry, plus e's writes before the site, plus earlier children's
    writes. Its writes could not conflict with e's reads or writes.
  - The result was published as a new exact entry, so later TUs would hit it directly.
  - Children reading `__COUNTER__`-built names were checked at the counter value of the
    site.
- **Measured.**

  | | splicing | no splicing | no cache |
  |---|---|---|---|
  | misses, 150 TUs | 3966 (247 spliced) | 4215 | — |
  | `-E`, all TUs, `-j 6` | 4.07 s | 0.98 s | 3.91 s |
  | files differing from no cache | 101 | 0 | — |
- **Why it fails.**
  - **Little coverage.** Most candidate children are themselves invalid at the site
    because of their own guard reads: 41 k rejections against 247 splices. They would
    need splicing in turn, which means a layered macro state carried through a nested
    search.
  - **Expensive search.** Up to 64 variants per key and per site, each checked through
    hash overlays, costs far more than recording the miss. That held even with cheap
    early refusals:
    - no site guard currently undefined;
    - a read that only a child's `#undef` could satisfy.
  - **Not exact.** 101 TUs' output differed. This was not diagnosed, given the cost.
- **Conclusion.** Guard-variant misses stay recorded. The declaration cache (§6.4 item 3)
  is the next large win. The work-in-progress patch was not kept in the tree.

### 6.7 Declaration cache, first form: parser snapshots after the leading headers (2026-11-08)
- **Where the shared work is.** All 879 C TUs were logged at `-j 1` with their top-level
  regions, meaning headers included from the main file or `-include`d.
  - 30% of all tokens (178 M of 598 M) lie in a leading run of regions that an earlier TU
    started with too. That run is mostly the three `-include` files plus the first header
    (depth 4).
  - 41% lie in some cached top-level region.
  - 90% are replayed at some depth.

  Only 237 of 4750 prefix-trie nodes are reached by more than one TU.
- **Parser state.** The front end's state is index-addressed arrays plus one bump heap.
  Heap pointers survive in only a few places:
  - in `struct cc`: `tyhash`, `fnhash` and the scope directory pages;
  - `tag.f`, `cstr.s`, and the asm operand arrays.

  Locations sit in `sym`, `node`, `tag`, `field` and `label`. The parser state is a
  function of the token stream, and the same header-cache entry means the same tokens. So
  the state after a TU's leading regions is the same for every TU that starts with the
  same entries, apart from locations: the main file comes first in location space.
- **Design** (`ccs/vcc/ccsnap.c`).
  - **Trie.** It is keyed by (entry, `__COUNTER__` at its start), with one root per
    front-end option set.
  - **Events from pp.** pp reports region starts and ends in delivered-token positions
    (`pp_top_events`).
  - **Taking a snapshot.** At each file-scope declaration boundary that falls exactly on a
    region end, the TU's visit is counted. The second visit takes a snapshot, if nothing
    was diagnosed. The snapshot holds the used part of every array and of the heap, with
    pointers as heap offsets and locations relative to the first region's base. It is
    stored as 16 KB pages, shared with the nearest ancestor's where equal.
  - **Restoring.** When `tk[0]` is the first token of a replayed region whose trie child
    has a snapshot, pp drops the rest of the region (`pp_skip_top`). Its macros, files and
    counter were applied at the hit already. Consecutive skips coalesce into one restore:
    a copy, then a pass over the location fields.
  - **Pragmas.** One arriving while a restore is pending forces the restore first.
- **Exactness.**
  - With and without the cache (`VCC_NODC=1`): `-S` assembly and the
    `--vcc-layout-checks` output are identical for all 879 TUs in one batch process.
  - ASan is clean.
  - Parity, sema and layout runs are green.
- **Measured** (`-j 6`, load 5–8; 256 MB cap):
  - 834 of 879 TUs restore;
  - 98 M tokens skipped (16%);
  - `-fsyntax-only` 2.96–3.25 s / 17.7–19.4 s CPU, against 3.33–3.81 s / 19.9–22.8 s;
  - `-S` 3.13 s against 3.51 s.

  Snapshots and restores cost about 0.5 s of CPU in all.
- **Memory.**
  - Snapshots average 3.5 MB of their own. Sibling first headers share little, so
    sharing pages with the parent gains little.
  - The cap trades memory for coverage: 128 MB gives 55 M tokens skipped, 256 MB 97 M,
    512 MB 104 M, 1024 MB (690 MB used) 116 M.
  - The default is 256 MB (`VCC_DC_MAX_MB`).
- **Limits.**
  - Only the leading run is covered. The other 84% of tokens, including most of the
    replayed nested regions, are parsed as before.
  - The prefix is cheap state: about 2.3 MB per restore, mostly declarations. Skipping
    16% of tokens therefore saves only about 10% CPU.
  - The larger step is relocatable per-region declaration replay, which needs parser-side
    read sets (name lookups, tags) and identity remapping. That is an AST reader in all
    but name.

### 6.8 Is per-region declaration replay worth it? Measured on PostgreSQL (2026-11-09)
- **Setup.** The 1574 C compiles of `work/build-pg-clang` (as `bench/pg/speed.py`), one
  `vpp --batch -c` process at `-j 1`, cold, load 4–6; 1570 ok (the 4 failures are the
  SVE popcount file and `norm_test.c`, as before). Each row is two alternating runs that
  agreed within 1%:

  | | instructions | cycles | `cc_run` |
  |---|---|---|---|
  | no caches (`VPP_NOHC=1 VCC_NODC=1`) | 43.7 G | 12.3 G | 40.2 G |
  | header cache only (`VCC_NODC=1`) | 40.2 G | 12.5 G | 37.3 G |
  | both (default) | 35.8 G | 11.5 G | 32.4 G |
  | both, warm (`--warm 1`) | 25.5 G | 7.6 G | 23.5 G |
  | both, 1 GB declaration cache | 34.8 G | 11.7 G | 31.4 G |

  Emit is 4.3 G and the object write 1.3–1.6 G in every row.
- **Lazy bodies do not help here.** PostgreSQL's headers have few inline functions:
  31 k bodies are kept (1.0 M tokens; `VCC_EAGER=1` makes no measurable difference), against 265 M tokens
  in the kernel. What is left in the headers is declarations.
- **Headers are most of the front end.**
  - The parser receives 104.5 M tokens (`--vcc-tokhash` counts). Stubs that keep only the
    main file's directive lines (plus `-iquote` of its directory) receive 93.3 M, so
    ~89% come from headers.
  - Without caches the stubs cost 32.3 G of the full run's 40.2 G in `cc_run`, so
    headers are ~80% of the front end.
  - With today's caches that is ~24 G, ~68% of all instructions. The declaration cache
    skips 27.9 M header tokens (981 restores, 256 MB cap reached), so ~65 M are still
    parsed.
  - (The stubs are pessimistic with the header cache on: `-iquote` per source directory
    splits the search-path signature. The split is therefore taken from the no-cache rows.)
- **The token header cache does not pay in time on PostgreSQL.** It saves 8% of
  instructions but costs 2% more cycles: replay is memory-bound. The decl cache gives
  the gain (−11% instructions, −8% cycles).
  - Its hit count looks low (46.7 k hits against 47.8 k misses, 38 k of the misses
    "counter name or a child entry", mostly SDK headers). That count is misleading:
    every nested miss is counted, but only one hit per replayed subtree. In bytes, 75 MB
    are replayed against 17 MB recorded (81%).
- **Profile** (`sample`, `bench/vccd-sample.py`, idle `__ulock_wait` left out):
  preprocessing pulled by the parser 42%, parse + sema 30%, emit 10%, object write 8%,
  other preprocessing 7%. That matches the 1570-TU profile in `PLAN-fas2.md`.
- **Estimate for per-region declaration replay.**
  - Assume header work is done only on misses (~19% of the header text) and a hit costs
    ~15% of parsing it. Then 36 G → ~19 G instructions, about −45%.
  - The ceiling is the rest: main file + emit + write ≈ 14 G, −60%.
  - A hit would also skip token replay, which removes the header cache's cycle cost.
  - This is compile CPU. In the kernel build, wall time is set by make (`PLAN-fas2.md`).
- **Conclusion.** On PostgreSQL, a codebase with ordinary headers, declaration replay per
  region is the largest remaining lever in the compiler, larger than in the kernel,
  where lazy bodies already took ~45%.

### 6.9 Design: declaration replay per region (2026-11-09)
Goal: a header-cache hit at a file-scope declaration boundary adds the region's
declarations to the parser state without delivering or parsing its tokens. The result
must equal parsing it: the same arrays in the same order, so that objects stay
byte-identical with the feature off (`VCC_NODR=1`). The prefix snapshots (§6.7) stay.
They are a cheaper bulk form for the leading run.

**Unit and key.**
- A *decl entry* hangs off a header-cache entry (`hent`) and is keyed by
  (`hent`, `__COUNTER__` at its start, front-end option signature). The `hent` already
  fixes the tokens, the macro reads and the files.
- The option signature must cover every parse-affecting option. Derive it from one
  struct, which also fixes the `TODO.md` item "Header/snapshot cache keys incomplete".
- It is published once (release/acquire), immutable and never freed, like the
  header cache, with a memory cap like `VCC_DC_MAX_MB`.

**When a region qualifies** (checked at record time; anything else is parsed as today).
- It starts and ends at a file-scope declaration boundary:
  - depth 0, no open struct, enum or initializer;
  - `ntk == 1`, with the region's first token in `tk[0]`;
  - the end right before the next declaration (`dc_boundary_slow`'s test, for regions
    at any depth).
  - This rules out x-macro includes inside arrays and enums (PG's `kwlist.h`, `*.def`
    patterns).
- No diagnostics (`ndiag`, `errors`, pp errors unchanged).
- No front-end pragma (`pack`, `weak`) consumed, and no `dc_kill`.
- Every read resolves to builtins or to entities created by regions. A read of a
  main-file entity makes the region uncacheable for that recording.

**What a region does to the state.** These are the parts the entry must reproduce.
- *Appends*, 1:1 and in order:
  - syms, tags, nodes and spans;
  - lists, ipool and fpool, strs, attrs, asms;
  - toplevel and tlasm;
  - lzt/lzf/lzs (lazy bodies, usually `lzfn.hc`: tokens referenced in the `hent`);
  - heap bytes (fields, string bytes).
- *Type interning*: `ty_intern` dedups against the whole table, so which types a
  region appends depends on the types that already exist. The entry therefore stores
  the region's interning calls, the first call per structural type and in order, as
  structural types (`ctype` with its base/aux/params as references). Replay
  re-interns them in that order.
  - That gives the same `ty` array as parsing, provided the pre-state is the same.
  - The pre-state is the same, by induction over exact earlier replays.
  - Records and enums (`ty_new`) always append.
- *Bindings*: each own sym that was bound at file scope is re-bound in order
  (`sym_bind`). Its `shadow` comes from the consumer's slot, which the reads validated.
- *Writes to external entities*:
  - a redeclaration (`declare`'s `prev`: composite type, `sc`, flags, attrs);
  - a tag completed here but declared earlier (fields, size, flags);
  - `typedef_name`, `TF_TRANSPARENT`, `cplx[]`.
  - Each is stored as the entity's full record after the region, with its index
    fields as references. Replay overwrites it. The read set guarantees that the
    pre-state of that entity was the recorded one.

**References.**
- Every index field in a stored record is either *own* (an offset from the region's
  start counts: `nsym0`, `ntag0`, `nnode0`, …) or *external*.
- An external reference names an entity by how the region reached it, which is a read
  (below), so replay maps it to the consumer's index.
- Builtins (`cc_init`, fixed per option set) are their own class with fixed indices.
- This needs a schema of every index field:
  - `sym` (`ty`, `body`, `attrs`, `next`, `ovl`, `psyms`);
  - `tag`/`field`, `ctype` (`base`, `aux`, `n` for a VLA node);
  - `node`: a/b/c by `op`, as `N_*` documents;
  - `attrs` (`cleanup`, string indices), `casm`, `lzfn`.
- The same schema serves locations: own locations are relative to the region's
  location base, as in ccsnap's `LOCS`.

**Reads: when an entry is valid.** The pp side is already validated by the `hent` hit.
On the parser side:
- *Name lookups* (`lookup`, `lookup_tag`, the `prev` lookup in `declare`,
  `typedef_id`) of ids not bound by the region itself.
  - Record (namespace, id) → the binding's *provenance*, or "unbound".
  - Hooked in `lookup`/`lookup_tag` behind one `c->rec` test.
  - Unbound reads matter: a typedef name appearing later changes the parse. They are
    the parser's analogue of pp's `ru` reads.
- *Provenance*: each sym and tag carries (`decl entry` that created it, its own index
  there), or BUILTIN, or MAIN (never valid in a replay read).
  - It is set on append and on replay.
  - It lives in a side array (`c->prov`, 8 bytes per sym/tag), so `sym` keeps its size.
- *Mutable state*: an entity read through a lookup may have been changed after its
  creation by a later region (redeclaration, tag completion). A per-entity *version*
  (the last writing decl entry) is part of the read: (provenance, version).
- *Tags reached without a lookup*: in `typedef struct foo foo_t;`, then elsewhere
  `struct foo {…};`, then `sizeof(foo_t)`, the third region depends on the second, but
  only looked up `foo_t`. Completion is monotone (incomplete → complete once, then
  immutable).
  - At record time, add (tag provenance, completing entry, or "incomplete") for every
    external tag reachable from the types of the external syms the region read.
  - Follow `base` through pointers, arrays and functions, and the fields of complete
    tags.
  - Use a visited bitset per region, and stop at tags that were complete before the
    region started *and* are reachable only through complete tags, since their state
    cannot differ.
- Validation of a hit is one `dget` per recorded name plus a provenance compare, and one
  compare per tag. Expect tens to a few hundred per region, against ~350 instructions
  per token parsed today.

**Replay** (`dr_apply`, in a new `cc/replay.c`):
1. Validate the reads, as above. On failure, parse the region as today.
2. Re-intern the types in order. This builds the region-local → consumer type map.
3. Bulk-copy the own records into the arrays and the heap, with one relocation pass
   driven by the schema: own += base, external → map, locations += region base.
4. Apply the external writes. Re-bind the file-scope syms. Append to toplevel and tlasm.
5. Bump the versions of written entities to this entry, and set provenance on the new ones.

On the pp side, the hit has already applied the macros, files and counter. Dropping the
tokens needs `pp_skip_top` generalized to a nested replayed region (`pp_skip_region`).
The cc side needs the boundary test above at the region's first token: an event from
pp at a region start, at any depth, as `pp_top_events` gives for the top level.

**Recording**, on a header-cache miss whose region qualifies:
- At region start, save the counts and set `c->rec` (lookups log reads; `ty_intern`
  logs calls; writes to external entities mark them in a bitset).
- At region end, if still qualified, encode the decl entry and publish it on the
  `hent`.
- Nested regions record independently. A parent's entry covers its children's effects:
  its own records include theirs, since they are appends in between, and its read set
  is the union minus what the children's own records bound, as pp's `hkid` composes.
- Cost: only on misses, which already pay ~15–20% for pp recording.

**Exactness tests.**
- `VCC_NODR=1` A/B:
  - objects byte-identical for all PG and kernel TUs (`vcc-cbatch.py`, `bench/pg`);
  - `-S` and `--vcc-layout-checks` identical;
  - vcc-sema run/layout 878/879, ctests, fpcheck, diag;
  - ASan, and TSan (shared entries).
- A generator in the style of `vcc-bflayout.py`, with random header DAGs:
  - forward tags completed in another header;
  - typedefs that shadow, or appear between two includes;
  - redeclarations with composite types;
  - enums whose values use earlier constants;
  - the same headers in different include orders and guard states.
  - Each TU compiled cold, and after warm TUs that recorded other orders.
  - Output compared with `VCC_NODR=1`, and layouts with clang.
- The "unbound read" and tag-completion cases each get a minimal ctest that fails if
  the corresponding validation is removed.

**Order of work.**
0. *Measure coverage first.* Count the header-cache hits (any depth) that start and end
   at a file-scope boundary, and their tokens, on PG and the kernel. Stub that every
   qualifying hit is replayed for free, without changing the output, to bound the gain.
   If the boundary tokens are well under the 65 M parsed header tokens on PG, rethink
   the unit before building.
   - *Done (2026-11-09).* `VCC_DRCOV=1` makes pp log every header-cache hit (any depth)
     as [first token, end) in delivered positions, and cc log every file-scope
     declaration start (with `ntk == 1`). The two are matched at the end of the TU, and
     `vpp --batch` prints the sums. This is the necessary condition only: diagnostics,
     pragmas and main-file reads inside a region are not measured yet (step 1).

     | | delivered | dc-skipped | in hits | hits at boundaries | of which top-level |
     |---|---|---|---|---|---|
     | PG, `VCC_NODC=1` | 103.0 M | — | 74.9 M | 73.4 M (27 247 of 27 947) | 56.9 M |
     | PG, default | 75.1 M | 27.9 M | 47.1 M | 46.0 M (25 708 of 26 383) | 29.5 M |
     | kernel, `VCC_NODC=1` | 597.3 M | — | 541.4 M | 541.4 M (36 065 of 36 095) | 228.4 M |
     | kernel, default | 491.8 M | 105.5 M | 435.6 M | 435.6 M | 123.2 M |

     ("Delivered" counts lazy bodies skipped in O(1), ~313 M of the kernel's tokens.)
   - *Reading.* Almost every hit starts and ends at a boundary: 98% of hit tokens on
     PG, ~100% in the kernel. Headers are included between declarations, and the few
     x-macro includes are small.
     - On PG, beyond the prefix snapshots, 46.0 M of the 75.1 M tokens still delivered
       (61%) are in such hits.
     - The rest (~29 M) is the main files (~11 M) and the own text of header-cache
       misses (~18 M). Those misses are mostly guard variants: step 4, or nothing.
     - 16.5 M of the 46 M lie in nested hits, inside a top-level region that missed.
       Step 4's "child inside a parent that missed" is therefore not an extra; nested
       hits must be replayable from step 2 on, which the boundary test already allows.
     - This is in line with §6.8's estimate, which assumed 81% coverage of the header
       text. The unit stands.
   - The instrumentation stays (a `c->drb` test per file-scope declaration, a
     `pp_drcov` test per hit; `vbench gate` +0.1% on fe/cc). It measures coverage at
     each later step.
1. Record-only mode with stats (reads, types and records per entry, bytes). Nothing is
   replayed yet.
   - *Done (2026-11-09), `cc/replay.c`, `VCC_DRREC=1`.*
     - **Where.** Unlike the plan, entries are recorded on a header-cache *hit*, not
       on a miss. Hits never nest: children are spliced into the parent's replay, so a
       hit is one disjoint region with no composition. A region parsed at a hit is
       recorded once, and later hits of the same (`hent`, counter) replay it. That is
       ccsnap's second-visit rule, per region.
     - **How.** A recording opens at a boundary where pp's open hit starts
       (`pp_open_hit`), and closes where that hit ends at a boundary. During it:
       - `lookup`/`lookup_tag` log names outside the region, bound or unbound;
       - `ty_intern` works on a table of the region's own, so the first use of each
         structural type reaches `dr_intern`, which is exactly the type log;
       - `ty_ptr`'s cache ignores pointer types older than the region (`ptr_lo`);
       - `pragma` logs `pack`/`weak`;
       - writes to older entities are found by comparing a copy of the syms, tags,
         types and attrs from the start. That is slow, and only for measuring which
         fields a replay must carry.
     - **Exactness.** Objects are byte-identical with and without `VCC_DRREC=1`
       (PG 1570/1570, kernel 839/839). `vbench gate` ok: fe +0.33%, cc +0.40%, mostly
       the `lz_wm | dr_on` test in `lookup`, which a permanent recorder needs anyway.
       While a region is recorded no ccsnap snapshot can be taken (its type table is
       not in the heap), so `VCC_DRREC` runs see few restores. The PG "default" column
       below is from a run before that change, with the same hooks otherwise.

     | PG | `VCC_NODC=1` | default (beyond ccsnap) |
     |---|---|---|
     | regions closed at a boundary | 32 963 (70.2 M tokens) | 31 583 (45.6 M) |
     | diagnostics / main-file reads | 0 / 0 | 0 / 0 |
     | `#pragma pack` (none started inside a pack) | 1068 (15.2 M) | 814 (10.1 M) |
     | pure, no writes and no lazy bodies | 24 759 (16.2 M) | 24 687 (16.2 M) |
     | pure, writes to older entities | 5011 (14.8 M) | 4880 (13.8 M) |
     | pure, lazy bodies only | 2125 (24.1 M) | 1202 (5.5 M) |
     | distinct entries | 4762 | 4676 |

     The kernel (`VCC_NODC=1`): 36 556 regions (541 M tokens) close. Pragma regions
     are 27 (4.5 M); no writes, no lazy: 1.8 M; writes: 308.6 M; lazy bodies only:
     226.1 M; 3117 distinct entries.
   - **Per pure region.**
     - PG: reads 6.4 bound + 86 unbound, type log 4.5 old + 33 new types, 199 syms,
       9.6 tags, 94 nodes, ~29 KB (25.6 bytes per token).
     - Kernel: 17 + 217 reads, 11 + 142 types, 619 syms, 35 tags, 108 lazy bodies,
       ~96 KB (7.8 bytes per token).
     - Validating ~100–250 lookups against parsing ~1500–12000 tokens is cheap, as
       planned. Storing every distinct entry would take ~140 MB (PG) and ~300 MB
       (kernel), the same order as ccsnap's cap.
   - **Writes to older entities**, all regions (PG / kernel):
     - tag completions 4044 / 33 035, each with its type's size and align;
     - sym flags only 5580 / 4378 (`SF_DEFINED | SF_USED | SF_REF`, and in the kernel
       `SF_ADDR_TAKEN`, `SF_LAZY`);
     - a redeclaration's `plb`/`ple` 3323 / 4812, and `psyms` 1599 / 1758;
     - attrs 222 / 528.
     - No sym's type changes: the composite is always the same type. No other tag
       fields change.
     - So "overwrite the older record with the recorded one" covers them. The read
       set must include those entities' versions.
   - **Consequence for the order.** Plain regions are only 16 of the 46 M PG tokens.
     Writes (tag completion above all) and `#pragma pack` (with the pack state at the
     start as an input) are needed in the first replay step. Lazy bodies are needed
     for the kernel. Composition (old step 4) is not needed, since entries are taken
     at hits.
2. Replay with provenance and versions (side arrays), validation of the read set
   including reachable tag completion, external writes as record overwrites, and pack
   state as an input. A/B against `VCC_NODR=1` on PG and the kernel. Target: ~40 M
   of PG's 46 M.
3. Lazy bodies through `lzfn.hc` (the kernel's 226 M, PG's 5.5 M).
4. Fold ccsnap into this if per-region replay of the prefix is as cheap as a snapshot
   restore; otherwise keep both, and let a recording coexist with snapshots (the
   region's type table in the heap, or no snapshot inside a recording).

**Open questions.**
- Whether re-interning in call order really reproduces `ty` exactly. Step 1 settles
  the method: the region's own table makes the log exactly "first call per structural
  type, in order", including types no record references. Whether replaying that log
  gives the same `ty` array is checked in step 2 (A/B objects, plus a debug compare of
  `ty` after replay against a parse).
- Where region boundaries fall inside `extern "C"`-like wrappers. In C: SDK headers'
  `__BEGIN_DECLS` is empty, so this is probably not a problem.
- Size per entry: own records plus the type log. ccsnap's 3.5 MB per snapshot was for
  the whole state; one region should be KBs to tens of KBs.

## Sources
- clang: TokenLexer.cpp, PPMacroExpansion.cpp, Preprocessor.cpp, MacroArgs.cpp,
  DependencyDirectivesScanner.cpp (llvm-project `main`)
- [GCC `-ftrack-macro-expansion` memory](https://gcc.gnu.org/pipermail/gcc-patches/2010-December/301921.html),
  [PR 56746](https://gcc.gnu.org/bugzilla/show_bug.cgi?id=56746)
- [warp](https://engineering.fb.com/2014/03/28/open-source/under-the-hood-warp-a-fast-c-and-c-preprocessor/),
  [repo](https://github.com/facebookarchive/warp)
- [Prosser's algorithm (Spinellis)](https://www.spinellis.gr/blog/20060626/cpp.algo.pdf)
- [Bothner, GCC Compile Server (2003)](https://gcc.gnu.org/pub/gcc/summit/2003/GCC%20Compile%20Server.pdf),
  [Tromey, IncrementalCompiler](https://gcc.gnu.org/wiki/IncrementalCompiler)
- [zapcc](https://github.com/yrnkrn/zapcc), [clang-scan-deps (EuroLLVM'19)](https://LLVM.org/devmtg/2019-04/slides/TechTalk-Lorenz-clang-scan-deps_Fast_dependency_scanning_for_explicit_modules.pdf)
- [Measuring kernel compile times with clang (LPC 2020)](https://lpc.events/event/7/contributions/802/attachments/652/1192/Measuring_Kernel_Compile_Times_w__Clang.pdf),
  [Macro performance: Lexer and SourceManager (LLVM Discourse)](https://discourse.llvm.org/t/macro-performance-lexer-and-sourcemanager/65713),
  [SourceLocation space RFC](https://lists.llvm.org/pipermail/cfe-dev/2021-February/067664.html)
- [Molnar, fast kernel headers](https://lkml.org/lkml/2022/1/8/170),
  [Google, Linux kernel header optimization (LPC 2023)](https://lpc.events/event/17/contributions/1620/)
- [Cuik](https://github.com/RealNeGate/Cuik), [aro Preprocessor.zig](https://github.com/Vexu/arocc/blob/master/src/aro/Preprocessor.zig),
  [tinycc libtcc.c](https://github.com/TinyCC/tinycc/blob/mob/libtcc.c)
