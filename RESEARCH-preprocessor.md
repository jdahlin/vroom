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
