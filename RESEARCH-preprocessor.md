# Preprocessor: state of the art and design for vcc (step 2.3 / 2.4)

Research of 2026-11-07. Sources: three research passes (clang/GCC/tcc/warp code, papers,
LPC talks) and local measurements on the arm64 kernel build (`work/build-o0` flags, Homebrew
clang 23). Points the research could only infer, not verify, are marked *(unverified)*.

## 0. Overview: what vccd reuses across TUs (2026-11-09)

**The model.** Every layer follows one rule:

> The first time, do the work and note what it read. The next time, if what it
> read is the same, reuse the result instead of redoing the work.

- **Exact.** The check covers everything the work read, so the result equals a
  run without the cache. Objects are byte-identical with and without each layer.
- **The work only happens when a cache misses.**
- **Three layers**, each built on the one before (details in the sections named):

| layer | what is reused | keyed on (what it read) | on a hit |
|---|---|---|---|
| 1. header cache (§3.3, §6) | one `#include`: its tokens and macro effects | the macros it tested or used, `__COUNTER__`, search path | tokens replayed, not lexed or preprocessed; inline bodies kept unparsed (lazy) unless used |
| 2. prefix snapshots (§6.7) | the parser state after a TU's leading headers: vcc's automatic PCH | the sequence of header-cache entries | one copy, the whole prefix skipped |
| 3. declaration replay (§6.9, opt-in `VCC_DR=1`) | one later header hit: the declarations it created | its lookups into older declarations (names, types, tags) | records copied and relocated, not parsed |

Layer 1 saves preprocessing, layer 2 saves parsing the opening headers, and
layer 3 saves parsing the rest of the headers.

**What each layer saves.** The numbers come from these runs:
- whole process, `vpp --batch -j 1`, mean of two runs;
- units are G instructions / G cycles; cycles are noisy, about ±5%;
- "no caches" is `VPP_NOHC=1 VCC_NODC=1`; each later row adds a layer.

| | PG (1574 TUs) | kernel (840 TUs) |
|---|---|---|
| no caches | 49.5 / 14.6 | 157.1 / 44.2 |
| + header cache | 46.8 / 14.7 | 93.9 / 28.9 |
| + prefix snapshots (today's default) | 42.1 / 14.0 | 85.6 / 27.1 |
| + declaration replay | 39.1 / 13.8 | 81.3 / 27.0 |

- **Header cache, kernel: −35% of cycles.** Almost all of it is lazy inline
  bodies. The kernel's headers hold 249 M tokens of inline functions, and only
  6.5 M of them are parsed. PG has few inline functions; there the cache saves
  instructions but no cycles: token replay is memory-bound.
- **Prefix snapshots: 4–6% of cycles.** They skip 40% of the kernel's
  front-end tokens (242 of 597 M) and 45% of PG's (46.5 of 103 M).
- **Declaration replay: 5–7% of instructions on top, but only 0–1.5% of
  cycles.** It replays 125 M kernel tokens and 18.6 M PG tokens. Parsing, with
  its high IPC, is traded for copies and cache misses.

**What is still done once per TU.** Largest first:
1. **Copying shared declarations into each TU's own state.** Both snapshots and
   replay copy them into each TU's arrays. This is the cycles that remain, and
   it is why replay saves instructions but not time. The way past it is to share
   a read-only layer instead of copying, as modules do: a different model from
   today's.
2. **Header-cache misses**, which preprocess the header again. PG: 48 k misses
   against 46.5 k hits, mostly guard variants and SDK headers. Kernel: 14.5 k
   misses against 41 k hits.
3. **Header hits without a replay entry**, which are parsed in full: 27 k
   (kernel) and 21 k (PG). Reasons: first sight, the gain rule said no, or the
   budget was spent. Half of the kernel's entries are never hit and take half
   of its budget.
4. **Code generation of `static inline` functions**, emitted in every object
   that uses them (`__refcount_sub_and_test` 143 times). Copies beyond the
   first are 0.5 of the kernel's 8.3 MB of function code (6%); emit is about 7%
   of the instructions, so this is under 1% of the total.
5. **The main files**, parsed once each. That is not duplication, only what is
   left.

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
   - *2a done (2026-11-09): exact replay, validated structurally; no provenance yet.
     Opt-in, `VCC_DR=1`; off by default.*
     - **Entry** (`cc/replay.c`, recorded at a hit as in step 1, published to a
       process-wide table keyed by (`hent`, counter, pack depth and top, option
       signature), up to 8 variants per key). It holds:
       - the reads: (namespace, id, A's sym or 0);
       - W, the older records they reach, copied at first use: syms, tags with their
         fields, types with their params, attrs, strings, the psyms/enum lists;
       - the type log (`ty_intern`, `ty_new`, `ty_func` in call order; `ty_func`
         works on a region-own table too);
       - the own records in A's indices, with heap pointers turned into offsets into
         a heap blob of their own;
       - the writes to W as (record, changed fields, post value);
       - the file-scope binds, the toplevel additions and the last token.
     - **Schemas.** One field table per record kind (sym, tag, field, ctype, attrs,
       node by `op`, list elements by referrer) says what each field holds: value,
       location, sym, tag, type, node, list, pool, string, attrs. One table drives
       recording W, the publish check, validation and translation.
       - A publish-time pass checks that every reference maps: own, builtin, or in W.
       - Unreferenced lists (declarator scratch) are copied as they are.
     - **Validation** at a hit in B: each read is looked up in B and unified with W
       (worklist, A→B map, injective for syms/tags/types). All of W must be reached;
       types that came only from interning are mapped by the type log instead.
     - **Apply**:
       1. replay the type log through B's interning;
       2. append the own records translated (index deltas, the type map, W slots,
          location base);
       3. set the final content of the region's records;
       4. apply the writes field by field;
       5. `sym_bind` the file-scope syms;
       6. append toplevel, set `prevloc`;
       7. `pp_skip_hit` drops the rest of the hit; `pp_skip_top` does it for a
          top-level one, so the decl cache sees the region end.
     - **Hidden inputs found.**
       - *Pack depth.* `lazy_ok` refuses inside any `#pragma pack`, so the key holds
         the depth, not only the alignment in effect.
       - *An enclosing header being recorded by pp.* No lazy body comes from the
         header cache then. Regions are only recorded and replayed where
         `pp_skip_hit` would be allowed.
       - *Writes.* Struct completion writes size and align into the older record
         type, which is supported as a write (not for interned types). The step-1
         count missed this because it only looked at pure regions.
     - **Not published.** Diagnostics; `#pragma pack`/`weak` inside the region; lazy
       bodies, labels, asm, `_Complex` (step 3 and later); an older `S_VAR` with an
       initializer in W; anything unmapped. A key that failed twice is not recorded
       again.
     - **Verification.**
       - `VCC_DRCHECK=1` with `VCC_DR=1` parses each region that would replay and
         compares field by field with what the replay would write: 0 mismatches in
         ~18 000 PG regions (two runs) and 5361 kernel regions.
       - With `VCC_DRREC`, it also refuses regions that wrote to an older record
         outside W.
       - Objects are byte-identical with `VCC_DR=1` (PG 1570/1570, kernel 839/839),
         and so are the diagnostics.
       - Clean under ASan+UBSan on both.
       - The default path: parity 873/873 and 911/912; sema run, batch and layout
         878/879; ctests, stress and vccd tests green; `vbench gate` ok (fe +0.41%,
         cc +0.37% since the merge-base, steps 0–2a together).
     - **Numbers**, `vpp --batch -j 1`, cold process:

       | | PG default | PG `VCC_DR=1` | kernel default | kernel `VCC_DR=1` |
       |---|---|---|---|---|
       | instructions (jobs) | 36.5 G | 46.4 G (+27%) | 79.4 G | 97.9 G (+23%) |
       | regions replayed | | 17 392 (16.4 M tok) | | 5194 (0.9 M) |
       | recorded / published | | 5814 / 3849 | | 16 887 / 3504 |
       | entries | | 328 MB | | 185 MB |
       | biggest refusals | | lazy 3.3 M, pragma 6.4 M | | lazy 256 M tok |

       - Per replayed PG region (~940 tokens), validation takes ~27 µs and apply
         ~17 µs. Parsing it takes ~100 µs.
       - A cheaper replay does not show yet: record-only (`VCC_DRREC`, nothing
         applied) costs 47.6 G, and replaying instead saves 1.2 G of that.
       - The overhead is recording plus validation:
         - the deep unify of W runs per try (32 282 tries for 17 392 hits);
         - `pair`/`pair_w` plus the inlined validation are ~30% of a sampled PG
           run's busy time;
         - the kernel's W copies at reads take 1.2 s (big structs reached through
           pointers).
   - *2b done (2026-11-09): cheaper validation, apply and publish. Still opt-in.*
     - **A 2a bug.** `rd`/`wr` handled field sizes 1, 2, 4 and 8 only. So the tag range
       `align..typedef_name` (12 bytes) and `field.width..bitfield` (3 bytes) read as 0:
       - a replayed struct completion never wrote `align`, `maxalign` or
         `typedef_name`;
       - bit-field widths were never compared;
       - `VCC_DRCHECK` compared the same zeros, so it could not see it.

       Objects stayed identical because codegen reads the type's `align`, not the
       tag's. Now every field up to 8 bytes is read, and `schema_check` aborts on a
       wider one (attrs ranges split). It was also behind ~5000 variants that
       failed to validate, on tag `align`.
     - **What validation compared.** On PG, 96% of the syms unified per try were
       enumerators: 3.3 M of 3.4 M, reached through enum tags such as `NodeTag`.
       Fields of tags came second (1.4 M). Half the work was in tries that failed.
     - **Provenance, as designed, did not pay.** Tags got (entry, own index, last
       writer), with hooks at every tag write (`DR_TAGWRITE`), and snapshots carried
       it. But the same header text gets a new identity in each snapshot, variant or
       parse: only 27 k of 226 k tag compares certified, and syms unified barely
       dropped. Identity of creation is the wrong key when equal content is created
       many times.
     - **Content memo instead.** The values of a W tag (the tag, its fields and its
       enumerators, without references and locations) are interned process-wide at
       publish (1081 distinct contents, 0.5 MB on PG).
       - A TU remembers, per tag, the content a successful validation compared it
         with. The memo is cleared by `DR_TAGWRITE` (struct and enum bodies,
         pre-body packed/aligned, `transparent_union`, `typedef_name`), by replayed
         writes and by a restore.
       - A tag that matches its memo has only its references paired. Builtin types
         and zero references are part of the content, so only the others are
         paired. Its enumerators are mapped by stored W positions, uncompared.
       - Equality is always established by a full comparison at least once per TU.
       - The recording TU sets the memo too, so a tag is hashed once per TU.
       - `DR_TAGWRITE` also refuses a region that writes an older tag outside W, a
         write the entry would miss (before, only `VCC_DRCHECK` saw that).
     - **Cheaper per try.**
       - W lookups are a hash per kind, not a sorted array with binary search (this
         also drops a `qsort` per publish).
       - A first pass over the reads checks binding, kind and flags before any deep
         compare.
     - **Apply.** Own records stay in A's indices in the entry. A relocation list
       built at publish (`rel_build`, which also replaces the publish-time mapping
       check) names the fields that change: own delta, W slot, own or W type,
       shadow to 0. Locations are one fixed pass per array, so apply is `memcpy`
       plus those.
     - **Recording less.**
       - A key that already has 8 variants is not recorded (each such recording was
         thrown away).
       - A region refused for its own tokens (pragma, lazy body, asm, label,
         `_Complex`) is not recorded a second time.
     - **Verification.**
       - `VCC_DRCHECK`: 0 mismatches (PG 18 324 regions, kernel 5508).
       - Objects identical with `VCC_DR=1`: PG 1570/1570, kernel 840/840.
       - ASan+UBSan clean (400 PG and 300 kernel TUs with `VCC_DRCHECK`).
       - Default path: parity 873/873 and 911/912; sema run, batch and layout
         878/879; ctests, stress and vccd tests green; the file-by-file build
         compiles; `vbench gate` ok (fe +0.36%, cc +0.33% since the merge-base).
     - **Numbers**, PG `vpp --batch -j 1`. Main's `f0ffb6a` (designated-initializer
       overrides) made 25 TUs with big initializer tables far slower in `il_run`
       (`gram.c` 0.5 → 247 G instructions), so they are left out: 1549 TUs, default
       39.2 G.

       | | 2a (bug fixed) | 2b |
       |---|---|---|
       | instructions, `VCC_DR=1` | 47.5 G | 41.7 G |
       | validate / apply / publish / W, ms | 440 / 290 / 300 / 125 | 167 / 85 / 243 / 103 |
       | regions replayed | 17 394 (17.4 M tok) | 17 394 (17.4 M tok) |
       | syms unified in validation | 3.4 M | 0.5 M |

       Replay is still 6% above parsing everything. What is left is mostly
       recording: the W copies at reads and publishing (~80 KB per entry, 338 MB on
       PG), about 23% of the front end's samples with the replay itself.
     - **Next.** Step 3 (lazy bodies) for the kernel's 256 M refused tokens. Cheaper
       recording (W reached lazily, entries smaller) before turning it on by
       default.
   - *2c done (2026-11-09): replay pays on PG (−2.9% of `cc_run`). Still opt-in.*
     - **Measured per phase** with thread instruction counters around each phase
       (debug code, not kept). Process totals moved ±1% between runs on the loaded
       machine, so from here on the metric is `cc_run` instructions from `vpp --batch`'s
       `perf:` line (the job threads only).
       - Recording a region cost ~230 k instructions in hooks and W copies
         (~33% on top of parsing it).
       - At publish, `rel_build` took 1.24 G for 27 M `rel_one` calls: one per
         field, values included.
       - Validation made 19.6 M `pair` calls, ~16 per W record, mostly on values.
       - 1867 of the 3509 published entries were never used: 206 of 328 MB.
     - **Changes.**
       - *Record at the second sight.* The never table also counts sights per key,
         and a key is recorded at its `DR_SEEN`-th sight (2). Recording at the third
         gave the same `cc_run` with 174 MB instead of 224.
       - *Stop a doomed recording at once.* When a lazy body, pragma, label, asm or
         weak pragma appears (checked at each read), the recording stops there
         instead of copying W until the region's end. On the kernel, 70.6 M of
         recorded tokens became 0.9 M.
       - *Value masks.* Per schema variant (sym: tag or not; ctype: 32 by kind and
         flags), a mask covers the bytes of the `K_IMM` fields. Validation compares
         a sym, field or type's values with masked word compares and pairs only the
         references; 0/0 references are skipped. Tag contents are keyed the same
         way (masked words plus normalized references).
       - *Specialized publish.* `SYM_REFS`/`FIELD_REFS` list the reference fields,
         and `schema_check` aborts if they drift from `sym_fd`/`field_fd`.
         `rel_build` visits only those fields (nodes: ty, loc and the three
         operands) with constant schemas.
       - *Delta relocations as offset lists.* Own deltas of 4 bytes are stored per
         array and op as offsets (4 bytes instead of 12), and apply skips an op
         whose delta is 0 (all deltas are 0 in a third of PG's replays).
       - *Tries ordered by hits.* A key's variants are tried by successful
         validations, most first (27 704 → 23 152 tries before the second-sight
         change).
       - The stamp tables are sized once per try, not at every pair.
         `dr_close` is called only while a recording is open (`dr_on`).
     - **Numbers**, `vpp --batch -j 1`, `cc_run` instructions:

       | | default | 2b `VCC_DR=1` | 2c `VCC_DR=1` |
       |---|---|---|---|
       | PG (1549 TUs) | 33.66 G | 36.37 G (+8.1%) | 32.68 G (−2.9%) |
       | PG entries | | 3509, 338 MB | 2247, 224 MB |
       | PG regions replayed | | 17 394 (17.4 M tok) | 16 033 (15.6 M tok) |
       | kernel (840 TUs) | 81.84 G | 99.10 G (+21%) | 84.62 G (+3.4%) |
       | kernel entries | | 3668, 214 MB | 1065, 49 MB |

       - PG ms, validate / apply / publish / W: 233 / 122 / 349 / 138 in 2b, and
         ~100 / 75 / 140 / 80 in 2c.
       - On the kernel, replay still loses: most of its regions hold lazy bodies
         (only 0.6 M tokens replay). With nothing recorded at all, replay mode
         costs ~1% for the bookkeeping at each top-level declaration.
     - **Verification.**
       - `VCC_DRCHECK`: 0 mismatches (PG 17 808 regions, kernel 4380; under
         ASan+UBSan, 400 PG and 300 kernel TUs).
       - Objects are identical with `VCC_DR=1`: PG 1545/1545 (`pgf.jobs`), kernel
         840/840.
       - Default path: the file-by-file build compiles; ctests, stress, sema,
         parity, vccd tests and `vbench gate` are as in the commit message.
     - **Next.** Step 3 (lazy bodies) is what the kernel needs. Then decide whether
       to turn replay on by default for PG-like code.
3. Lazy bodies through `lzfn.hc` (the kernel's 226 M, PG's 5.5 M).
   - *Done (2026-11-09), still opt-in. The measurements now use all 1574 PG TUs,
     since main's `8e5c987` made initializer lists linear again.*
     - **Lazy bodies.** In a region replayed from the header cache, a lazy body is
       always `hc`: its tokens stay in the cache entry `z.e`, which is immutable and
       the same in every TU that replays the region. An entry therefore keeps the
       region's `lzfn` records, and apply appends them (`lz_tr`):
       - sym and wm move by the sym delta, psyms by the list delta, and `tk` is B's
         `nlzt`;
       - the locations and `z.base` move by the location delta;
       - their parameter lists are marked `K_SYM`.
       - A body kept as tokens of its own (`lzt`/`lzs` grew) still refuses the
         region.
       - `VCC_DRCHECK` compares the records field by field: 0 mismatches (PG 19 149 regions, kernel 11 491; ASan+UBSan 400 PG + 300 kernel TUs), and objects are identical with `VCC_DR=1` (PG 1570/1570, kernel 840/840).
     - **Bugs this exposed.**
       - *Quadratic early check.* The early check (`own_fail` at each read) scanned
         all of the recording's lazy bodies, which was quadratic in kernel regions
         with thousands of them. It now checks only the new ones.
       - *Budget.* With the 512 MB budget spent, every recording still ran to the
         end and was then dropped (4037 times on the kernel). No recording starts
         once the budget is spent.
     - **Worth it?** The replayed kernel tokens are mostly lazy bodies, which
       parsing skips cheaply anyway. A kernel region's W is large, so many replays
       cost more than parsing. So publish now estimates both sides:
       - saving: the tokens outside lazy bodies × `DR_TOKCOST`;
       - cost: W records × `DR_WCOST`, own records × `DR_OWNCOST`, plus
         `DR_FIXCOST`;
       - the region is published only if the saving is at least `VCC_DR_GAIN`/10
         of the cost; if not, the key is not recorded again.
       - The constants are fitted by a sweep. Gain 1×, 2×, 4×, 8× and 16× of the
         first estimate gave kernel 82.79 / 82.37 / 81.45 / 82.05 / 83.10 G and PG
         33.98 / – / 33.83 / 34.00 / 34.93 G; the best (4×) is now the default.
       - A bigger budget made it worse: 2 GB gave the kernel 85.5 G.
     - **Numbers**, `vpp --batch -j 1`, `cc_run` instructions:

       | | default | 2c | 3 |
       |---|---|---|---|
       | PG (1574 TUs) | 36.12 G | 34.64 G (−4.1%, before the gain rule) | 33.83 G (−6.3%) |
       | kernel (840 TUs) | 82.68 G | 84.62 G (+3.4%; default then 81.84) | 81.45 G (−1.5%) |

       On the kernel, 8266 regions replay (105 M tokens, mostly lazy bodies). On
       PG, 7231 regions replay (19.6 M tokens).
     - **Next.** Entries are large on the kernel (~190 KB, mostly W), and W's
       validation is what limits the kernel. Make W cheaper per try, e.g. a TU memo
       for syms and types like the one for tags, or record W lazily.
   - *3b (2026-11-09): what costs on the kernel, measured before building a W memo.*
     - **Validation was no longer the problem.** Per run, the kernel spent
       validate 170 ms, apply 179 ms, publish 491 ms and W copies 270 ms; PG spent
       72 / 118 / 126 / 55 ms. A TU memo for syms and types would save part of
       validation, under 1% of `cc_run`, so it was not built.
     - **Where the cost was.**
       - *Late gain check.* The gain rule was applied after building the entry:
         2032 kernel recordings were recorded, translated and relocated, and
         then dropped. It now runs before anything is built (`dr_pays`).
       - *Hopeless recordings ran on.* During a recording, W alone can outweigh
         the region's whole token count (`pp_hit_ntok`: the hit's tokens with its
         children's). Such a recording now stops at once (as "no gain").
       - *"Unmapped" was a bug.* 472 kernel recordings (14.9 M tokens) failed
         because the parameter list of `f(void)` is an empty list at exactly
         `l1`, the region's list end, which counted as neither own nor W. Own lists
         are now `l0 ≤ v ≤ l1`, in `rel_one` and `tr`. DRCHECK found no
         mismatches with the fix.
     - **Kernel entries**, 512 MB for 1978 entries:
       - own syms 175 MB (lazy functions and their parameters);
       - the type log 52 MB, W tags with fields 44 MB, lazy bodies 40 MB,
         relocations 38 MB, W types 25 MB.

       A bigger budget no longer pays: 1 or 2 GB gave 79.6–79.9 G, against 79.6 G
       with 512 MB.
     - **The gain constants re-fitted** after these changes. Twice the cost
       estimate gave kernel 78.67 G and PG 33.15 G; the old estimate gave
       79.56 / 33.17; 4× gave 79.25 / 34.08. The doubled estimate is now the
       default.
     - **Numbers**, `vpp --batch -j 1`, `cc_run` instructions:

       | | default | 3 | 3b |
       |---|---|---|---|
       | PG (1574 TUs) | 35.90 G | 33.83 G | 33.27 G (−7.3%) |
       | kernel (840 TUs) | 82.68 G | 81.45 G | 78.86 G (−4.6%) |

       PG run-to-run noise is ~±0.4 G (default measured 35.90 and 36.12 G).
     - **Verification.**
       - `VCC_DRCHECK`: 0 mismatches (PG 13 967 regions, kernel 9700; ASan+UBSan
         on 400 PG and 300 kernel TUs).
       - Objects are identical with `VCC_DR=1`: PG 1570/1570, kernel 840/840.
       - The default path: suites and gate green (see the commit).
     - **Next.** Apply and publish now cost more than validation. Possible steps:
       - smaller entries: store the type log's final content only for `OP_NEW`,
         and fold the location passes into the offset lists;
       - more replay coverage: on PG, 20 833 hits found no entry, most of them
         keys the gain rule turned down.
   - *3c (2026-11-09): cheaper publish, by building less of it.*
     - **Measured per phase** (instructions, temporary counters, kernel / PG).
       - Publish: relocation lists (`rel_build`) 780 / 183 M, own records
         305 / 71 M, W tag values 212 / 68 M, the type log 109 / 22 M.
       - Apply: own records (copy, relocations, location passes) 428 / 303 M,
         the type log 298 / 94 M, bindings 217 / 93 M.
     - **Most entries are never used.** Hits per published entry:

       | hits | 0 | 1 | 2–3 | 4–15 | 16+ |
       |---|---|---|---|---|---|
       | kernel entries (MB) | 678 (278) | 275 (74) | 240 (77) | 247 (65) | 118 (14) |
       | PG entries (MB) | 330 (43) | 133 (19) | 112 (14) | 142 (19) | 65 (5) |

       Recording later does not help. At the 3rd or 4th sight instead of the
       2nd, kernel 79.05 / 79.88 G and PG 33.57 / 33.67 G: the lost hits weigh
       more than the dead entries.
     - **Relocation lists at the first valid hit.** They need only the entry
       (W positions from its `wk` tables), so `rel_ready` builds them once,
       state in `drent.rst`. Another thread finding them being built parses
       that hit. An entry whose lists do not map is never applied; none were
       seen. Their bytes are counted when built, so dead entries leave more
       budget: the kernel publishes 1618 entries instead of 1558.
       `VCC_DRCHECK` still builds them at publish, so every recording checks
       they map.
     - **Apply was left as it is.** Per kernel replay it copies ~900 syms,
       ~1060 list elements and ~650 general relocations, at ~28 instructions
       per element. Splitting the type relocations into their own offset lists,
       or folding the location passes, would save under 0.1%.
     - **Gain constants unchanged.** Re-fitted: gain 7 / 10 / 15 / 20 gave kernel
       78.47 / 78.32 / 78.82 / 78.93 G and PG 33.26 / 33.35 / 33.92 / 34.12 G.
     - **Numbers**, `cc_run`: kernel 78.61 → 78.32 G (−0.4%). PG is unchanged:
       on the same machine state, 3b measured 33.34 / 33.42 G and 3c measured
       33.40–33.47 G. Against the default, kernel −5.3% and PG −7%.
     - **Next.** Coverage, not cost per replay:
       - the dead entries take half the kernel budget, and the budget ran out
         5700 times;
       - evicting entries without hits, or smaller own syms (175 MB), would let
         the useful ones in.
   - *Time, not only instructions (2026-11-09, at 3c).*
     - **Setup.** Whole process, `vpp --batch -j 1`, mean of two alternating
       runs, load 6–10. Units are G instructions / G cycles; cycles are noisy,
       about ±5%.

       | | PG (1574 TUs) | kernel (840 TUs) |
       |---|---|---|
       | no caches (`VCC_NODC=1`) | 46.8 / 14.7 | 93.9 / 28.9 |
       | prefix snapshots only (default) | 42.1 / 14.0 | 85.6 / 27.1 |
       | replay only (`VCC_NODC=1 VCC_DR=1`) | 40.5 / ~14.4 | 83.9 / ~27.2 |
       | both (`VCC_DR=1`) | 39.1 / 13.8 | 81.3 / 27.0 |

     - **Instructions.** Replay alone saves more than the snapshots alone (§6.7,
       vcc's automatic PCH), and the two stack. On top of the snapshots, replay
       saves 7% (PG) and 5% (kernel).
     - **Cycles.** On top of the snapshots, replay saves only ~1.4% (PG) and
       ~0.2% (kernel), within the noise. Wall time at `-j 6` is noisy too: kernel
       2.26 → 1.97 s, PG no gain. The snapshots alone save 5–6% of cycles.
     - **Why.** Replay is memory-bound, as the token header cache was (§6.8). It
       replaces parsing, which has a high IPC, with copies, relocations and W
       validation over ~500 MB of entries.
     - **Consequences.**
       - Steps 2–3c were judged by `cc_run` instructions, and the gain constants
         were fitted to instructions. From here on replay is judged by cycles
         (alternating runs; `-j 6` wall time as a check), and a change pays only
         if it saves cycles.
       - The gain rule needs re-fitting against cycles.
       - Memory traffic is the cost. So next: evict entries without hits and
         shrink entries (own syms, W). Cheaper instructions per replay come
         after that.
       - `VCC_DR` stays opt-in until it saves time.
4. Fold ccsnap into this if per-region replay of the prefix is as cheap as a snapshot
   restore; otherwise keep both, and let a recording coexist with snapshots (the
   region's type table in the heap, or no snapshot inside a recording).

**Open questions.**
- Whether re-interning in call order really reproduces `ty` exactly. Step 1 settles
  the method: the region's own table makes the log exactly "first call per structural
  type, in order", including types no record references. Whether replaying that log
  gives the same `ty` array is checked in step 2 (A/B objects, plus a debug compare of
  `ty` after replay against a parse). *Settled in 2a: it does (`VCC_DRCHECK`). The
  count of new types may differ from A's, since B can already have some; the log
  maps each one.*
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
