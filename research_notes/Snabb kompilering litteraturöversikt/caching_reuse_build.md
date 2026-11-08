# Avoiding/sharing work across compilations: header reuse, caching, compile servers, build-level techniques

Context for the reader: vroom/vcc runs a persistent compile server (vccd) receiving ~2500 kernel TUs per cold build. "Cold" = no prior outputs, so persistent cross-build caches (ccache, Bazel) only give ideas; what matters is sharing *within* one build and not doing unneeded work at all. Researched November 2026; around 20 tool calls, so coverage is broad but shallow in places (see the Gaps sections).

## Sharing header work across TUs within one build (PCH, modules, zapcc, unity builds, CodeStore)

### Takeaway
Every approach that shares *parsed* header state across TUs (PCH, clang modules, zapcc) saves the most on C++ template-heavy code and least on C. zapcc turned caching off for C files entirely. Unity/jumbo builds give large real-world savings but break code semantics and were dropped by Chromium. For C kernel code, the biggest measured win came from shrinking what headers pull in (Fast Kernel Headers, below). Caching parsed headers was not the source of that win.

### Cited Findings
- **zapcc** (Clang fork, open-sourced in 2018): a server, zapccs, stays in memory and receives compile commands from clients; caching targets headers. It claimed "up to 50x faster recompiles, 2–5x faster full builds" — [InfoQ 2018](https://www.infoq.com/news/2018/06/zapcc-caching-cpp-compiler-open)
- zapcc's own docs (quoted by InfoQ) call plain C projects its worst case, with no acceleration; zapcc disables caching for C files. It ignores PCH. Its memory use is high and stays resident between builds. The last LLVM merge was r307021 (2017-07-03), and the project appears inactive — [InfoQ 2018](https://www.infoq.com/news/2018/06/zapcc-caching-cpp-compiler-open); [GitHub mirror](https://github.com/linecode/zapcc)
- Independent zapcc test (2016, header-heavy C++ library): about 3x faster than clang on debug builds, but only 1.2–1.4x on release builds (1.1–1.3x vs GCC). The author saw no speedup on a single-file change without killing the servers — [Baptiste Wicht 2016](https://baptiste-wicht.com/posts/2016/11/zapcc-a-faster-cpp-compiler.html)
- **PCH in clang**: a LibreOffice developer measured PCH saving about 2/3 of build time with MSVC but only about 10% with clang. -ftime-trace showed the saved parse time was offset by template instantiation repeated in every TU. `-fpch-instantiate-templates` (Clang 11, 2020) moves that work into PCH generation. Its author reported up to 20–30% build-time reduction; it is off by default because it fails on headers that are not self-contained — [cfe-dev 2020](https://lists.llvm.org/pipermail/cfe-dev/2020-January/064308.html); [Phoronix](https://www.phoronix.com/news/LLVM-Clang-11-PCH-Instant-Temp); [MaskRay 2023](https://maskray.me/blog/2023-07-16-precompiled-headers)
- The Lix project removed about 400 CPU-seconds (30%) by moving nlohmann/json instantiations into the PCH. This is a single-project number — [Lix commit](https://git.lix.systems/jade/lix/commit/af546be20560c5e192b8cf8df5d28c94ea7a9832)
- **Clang header modules in Chromium**: with a cold module cache, building with modules is "much slower" than without. With Chrome's clang plugins enabled, modules are slower even when fully cached — [Chromium docs/modules.md](https://chromium.googlesource.com/chromium/src/+/278fd13f92cd28348c76ddda3e47dbf94de7d8fa/docs/modules.md)
- A clang C++20 modules user saw build times *increase*. -ftime-report put half the time in reading modules (AST deserialization), and clang does not cache evaluated values across module boundaries — [LLVM Discourse 2023](https://discourse.llvm.org/t/modules-increased-build-times/68755)
- I found no rigorous public benchmark of C++20 named modules or header units against #include. Blog claims of 2–10x are unsourced — [cplusplus.com thread](https://cplusplus.com/forum/general/279701); [Discourse](https://discourse.llvm.org/t/modules-increased-build-times/68755)
- **Chromium jumbo/unity builds**: Blink adopted jumbo because non-goma build times had become "unsustainable" — [blink-dev](https://groups.google.com/a/chromium.org/g/blink-dev/c/F4JYqAY1dgM/m/b-hopZvmBgAJ)
- Dirk Pranke announced jumbo's removal on 2019-08-15. His reason was that it "interferes too much with the way people frequently write C++ code" (anonymous-namespace collisions and awkward workarounds); he pointed to Goma instead — [chromium-dev 2019](https://groups.google.com/a/chromium.org/g/chromium-dev/c/DP9TQszzQLI)
- A downstream packager (Fedora Copr) reported 8–10 h per Chromium build with jumbo vs 18–19 h without, and build targets going from about 25,000 to 41,000 — [copr-devel 2019](https://lists.pagure.io/archives/list/copr-devel@lists.fedorahosted.org/thread/E4L64OR7GEHL7MYNMWGPDXRJWQ3WN2NS/)
- Fast Kernel Headers also merged 20+ scheduler object files into 4 ("consolidated build units"), a small-scale unity build inside the kernel — [Molnar, LKML 2022 cover letter](https://patchew.org/linux/YdIfz+LMewetSaEB@gmail.com/)
- **IBM VisualAge C++ 4.0 / CodeStore** (late 1990s): a true incremental compiler that recompiled C++ function by function. It used a persistent code cache, the CodeStore, shared by the editor, debugger and browser. Source stayed in files, and "incorporation" replaced make, compile and link. Its research predecessor was Montana — [Nackman, Dr. Dobb's Dec 1997](https://archive.retro.co.za/CDROMs/DrDobbs/CD%20Release%2012/articles/1997/9712/9712k/9712k.htm); [IBM Research: Montana](https://research.ibm.com/publications/architecture-of-montana-an-open-and-extensible-programming-environment-with-an-incremental-c-compiler); [comp.compilers 2000](https://compilers.iecc.com/comparch/article/00-05-050)
- **Lucid Energize** (early 1990s): incremental compilation and linking at function and class level, built on a client/server central service and an object database — [Dr. Dobb's new products](https://www.drdobbs.com/new-products/184402584)

### Inferences
- For C, header cost is mostly lexing, preprocessing and declaration sema; no template instantiation is repeated. That is why zapcc and clang PCH gain little on C. A vccd-level share for C would have to cache macro and declaration state. Saving parse time alone is not enough.
- vccd differs from zapcc in a way that matters. A cold build has only one build to amortize over, so the cache must pay for itself during it: about 2500 TUs, which reuse the same headers heavily.
- Unity builds give the sharing for free (one parse per header per batch). They break `static` name collisions and macro leakage between files, which kernel C has plenty of. Using them would need per-TU scoping inside one process, which a server can emulate.

### Gaps
- No clang or GCC measurements of PCH or modules on C codebases, let alone the kernel.
- Microsoft IFC (the MSVC module binary format) and its build-time numbers were not researched.
- I could not find out why zapcc died beyond its going inactive (no maintainer statement found).
- "Compiling C/C++ with a persistent server" academic papers: not found in this pass.

## Preprocessor-level reuse (multiple-include optimization, PTH, token caches, conditional keys)

### Takeaway
GCC's multiple-include optimization is precisely specified and cheap; it is the baseline. Clang's PTH cached tokens, stat results and the conditional-block skeleton, mmapped read-only and shared between processes. It was removed in 2018 because it was broken and unmaintained, not because the idea failed. No source gave PTH speedup numbers.

### Cited Findings
- **GCC multiple-include optimization**: the whole file must be inside `#ifndef X`/`#define X` … `#endif` (or `#if !defined X`, with tokens taken straight from the source and no macro expansion while parsing it), with only whitespace and comments outside and no `#else`/`#elif` at the outer level. Detection is a small state machine (`mi_valid`, which any non-directive token resets). On re-include, if X is defined, no buffer is pushed: the file is neither reopened nor re-lexed — [GCC cppinternals: Guard Macros](https://gcc.gnu.org/onlinedocs/cppinternals/Guard-Macros.html)
- **Clang PTH** stored raw lexer tokens, `stat` results (to avoid syscalls when resolving `#include`) and the nesting of `#if` blocks (to skip inactive regions without lexing). It was mmapped read-only so concurrent `clang -cc1` processes shared pages. One file served several targets, and it was described as generated in "a small fraction of the time" of GCC PCH. Processing still scales linearly with header size, while full PCH is "theoretically constant time". No numbers were given — [Clang PTHInternals doc](https://bcain-llvm.readthedocs.io/projects/clang/en/latest/PTHInternals/)
- PTH was removed in r348266 (December 2018, Erich Keane). TokenKind was stored in 8 bits, so about a third of token kinds did not work, and the feature was unmaintained. The RFC was posted to cfe-dev on 2018-11-15 — [cfe-dev RFC](https://lists.llvm.org/pipermail/cfe-dev/2018-November/060168.html); [cfe-commits r348266](https://lists.llvm.org/pipermail/cfe-commits/Week-of-Mon-20181203/253425.html); [D54547](https://reviews.llvm.org/D54547?download=true&id=174212&large=true)
- PCH (in contrast to PTH) caches full ASTs plus preprocessor state, so it imposes a single-prefix constraint: the PCH must be the first include and defines must match — [PTHInternals](https://bcain-llvm.readthedocs.io/projects/clang/en/latest/PTHInternals/)

### Inferences
- Within vccd: PTH's design (per-file token arrays, a cached stat/include-resolution table, and a precomputed `#if` skeleton for jumping over dead blocks) maps directly onto an in-memory, build-scoped cache shared by all worker threads, with no serialization needed. That removes PTH's main costs (file format, version skew).
- To reuse *macro state* or *declarations* safely (beyond tokens), a header result needs a key: the set of macros the header tests (`#ifdef`/`defined`/`#if` identifiers) plus their values at entry. This is the same "external dependence" a guard check generalizes. The kernel's TUs share `-include` prefixes (kconfig.h, compiler_types.h) and the same autoconf.h, so most entry states should match. This needs measuring in vroom.

### Gaps
- Microsoft `/Yc`/`/Yu` internals (memory-image snapshot of compiler state) and measured gains: not researched.
- No published measurement of the hit rate of "same header, same relevant macro state" across kernel TUs; vroom would have to measure it itself.

## Caching tools and compiler-as-a-service

### Takeaway
ccache numbers show the cost spectrum: about 0.7 s compile, about 25 ms for a hit with preprocessing, about 5 ms for a hit with only hashing. Go and rustc show how to key reuse (an action ID hashed from inputs, or per-query fingerprints with red-green validation). For a cold build these mainly inform how a header or declaration cache *inside* vccd is keyed and validated.

### Cited Findings
- ccache 3.7.1, i5-4690K, SSD, median of 100 runs, for ccache.c (`-g -O2 -MD`): no ccache 0.6988 s; misses: preprocessor mode 0.7251 s, direct 0.7268 s, depend 0.7102 s; hits: preprocessor 0.0247 s, direct 0.0048 s, depend 0.0051 s. For a C++ file with 9 standard includes: no ccache 0.2421 s; misses 0.2892 / 0.2919 / 0.2495 s; hits 0.0496 / 0.0083 / 0.0083 s — [ccache performance](https://ccache.dev/performance.html)
- How the modes work: direct mode hashes the source, every included file and the options, and records the include hashes in a "manifest". It has a rare false-positive corner case, and the first compile still runs the preprocessor. Depend mode uses the `-MD` output, which gives "much lower cache miss overhead" (1–3% vs typically 5–20%) at a lower potential hit rate — [ccache.dev](https://ccache.dev/); [ccache list 2010](https://lists.samba.org/archive/ccache/2010q1/000452.html)
- The original direct-mode author reported a Samba rebuild taking 28 s with direct mode vs 100 s with the original (preprocessor-mode) ccache — [ccache list 2010](https://lists.samba.org/archive/ccache/2010q1/000452.html)
- **Go build cache**: each action has an action ID (hash of command line, environment, input file contents and tool identity, salted SHA-256) and a content ID (hash of the output). Storing the content ID inside packages and binaries avoids re-hashing large inputs. An installed artifact acts as a one-element cache. Tools are identified by `-V=full` output — [cmd/go buildid.go](https://go.googlesource.com/go/+/go1.16.4/src/cmd/go/internal/work/buildid.go); [cmd/go/internal/cache](https://golang.org/cmd/go/internal/cache)
- **rustc incremental (red-green)**: a query DAG with ordered edges is saved after each compile. try-mark-green replays a query's dependency reads in their original order, and if all are green the result is reused without re-execution. If a re-run produces an unchanged hash, the query is still green ("early cutoff"). Usually only result hashes are stored, with full results only for selected queries — [rustc-dev-guide](https://rustc-dev-guide.rust-lang.org/queries/incremental-compilation.html)

### Inferences
- The ccache hit costs (about 5 ms per TU, dominated by hashing headers and process overhead) are already above vroom's budget: 2 s × 10 cores / 2500 TUs ≈ 8 ms of CPU per TU *total*. A cold build cannot benefit from output caching anyway, which confirms that vroom must cut actual work.
- The ordered-replay idea from red-green maps onto header reuse. A cached header result is valid if replaying its recorded macro queries (defined? value?) against the current macro table gives the same answers, which is a cheap check.

### Gaps
- sccache, Bazel/Buck2 remote execution, distcc/icecc, Roslyn, salsa (as a library), Zig incremental compilation and Adapton were not researched (budget). Hashing-speed numbers (e.g. BLAKE3 vs xxh3 for ccache 4.x) were not collected.

## Build-system overhead (make/kbuild, ninja, process spawn, Fast Kernel Headers)

### Takeaway
Fast Kernel Headers (Molnar, January 2022) is the most relevant measured result. Cutting what headers pull in sped up a full kernel build by 78% and incremental builds by 112–173%; key headers' preprocessed size dropped 10–35x and kernel/pid.i about 2.6x. Make/kbuild has a fixed per-invocation overhead of seconds on no-op builds. Reported figures are about 2.25 s (make) vs 0.065 s (an experimental ninja generator) and about 2.8 s rising to 18 s in a v6.7 regression, which is in the same order as vroom's whole 2 s budget.

### Cited Findings
- **Fast Kernel Headers v1** (2022-01-02), stock x86 distro config with modules built in, `make -j96 vmlinux`, 3 runs: 231.34 ± 0.60 s → 129.97 ± 0.51 s (+78% builds/hour, 15.5 → 27.7); CPU time 11,475 s → 7,101 s (+61.6%) — [Molnar cover letter](https://patchew.org/linux/YdIfz+LMewetSaEB@gmail.com/)
- Incremental rebuild after touching one header: sched.h 230.30 → 108.35 s; mm.h 216.57 → 79.42 s; fs.h 223.58 → 85.52 s; device.h 224.35 → 97.09 s; net/sock.h 105.85 → 40.88 s — [same](https://patchew.org/linux/YdIfz+LMewetSaEB@gmail.com/)
- Preprocessed size of a header standing alone (LOC, with the count of indirect headers): linux/sched.h 13,292 (324) → 769 (64); fs.h 22,497 (427) → 1,993 (120); mm.h 26,727 (453) → 1,855 (133); tcp.h 60,133 (725) → 3,829 (153); net/sock.h 58,880 (715) → 1,543 (98); asm/pgtable.h 12,949 → 5,742. kernel/pid.i: 94,569 → 35,941 lines — [same](https://patchew.org/linux/YdIfz+LMewetSaEB@gmail.com/)
- Scope: about 2,200–2,300 commits, 25,288 files changed. Techniques: `per_task()` to take task_struct fields out of sched.h, `_types.h` splits, removing includes, over 100 uninlining commits, about 790 commits adding explicit dependencies, and `CONFIG_KALLSYMS_FAST` (objtool symbol table that avoids the triple link; x86-64 only). Supported architectures: x86, arm64, MIPS, Sparc. v2 (2022-01-08) reported about +88% elapsed and +77% CPU for a clang build — [cover letter](https://patchew.org/linux/YdIfz+LMewetSaEB@gmail.com/); [CNX Software 2022](https://www.cnx-software.com/2022/01/04/the-linux-kernel-could-soon-be-50-to-80-faster-to-build/); [LWN 880825](https://lwn.net/Articles/880825/)
- **Make no-op overhead**: a kernel developer measured a no-op kernel build at 2.254 s with make vs 0.065 s with an experimental ninja generator. Make's time was "pretty much all" parsing, and the no-op made over a million `stat()` calls. Attribution caveat: this came through search-result summaries of an LKML/openwall thread that I did not open directly — [lists.openwall.net 2017](https://lists.openwall.net/linux-kernel/2017/10/05/256)
- `checksyscalls.sh` runs on every `make`. Replacing spawned `cat` processes with a shell builtin made it about 13x faster and cut a single-file rebuild from 5.5 s to 4.5 s — [kernel commit](https://gem5.googlesource.com/arm/linux/+/d21832e212d2613c0a0f3e5f09967cb3402c8a53)
- Regression report (January 2024): a no-op `make bzImage` on x86_64_defconfig took about 3 s on v6.6 and about 18 s on v6.7 — [LKML 2024](https://lkml.iu.edu/2401.2/05544.html)
- **GCC vs clang on the kernel**: a 2020 Linux Plumbers talk found GCC faster than LLVM on arm64 and x86_64, even with LLVM built with LTO+PGO. Phoronix (September 2020) found clang faster only when PGO-built and targeting 32-bit ARM — [LPC 2020 talk](https://lpc.events/event/7/contributions/802/); [Phoronix](https://www.phoronix.com/news/GCC-Faster-Kernel-Builds-Clang)
- In 2021 Linus Torvalds reported a clang allmodconfig step at about 2.7 s vs 0.9 s for GCC; others measured 1.49–1.62 s vs 1.28–1.4 s and suspected HOSTCC=clang under LLVM=1 was confounding the comparison — [LKML 2021](https://www.lkml.org/lkml/2021/4/29/884); [lore](https://lore.kernel.org/CAKwvOdmMF_v9TzBtFn2S1qSS_yCDO8D-u3WhBehUM7gzjcdjUQ@mail.gmail.com/)
- Process creation: the posix-spawn library maintainers report fork(2) slowing as the parent's memory grows (page-table copying), while posix_spawn runs in constant time with POSIX_SPAWN_USEVFORK. They benchmarked Linux 2.6 and Mac OS X, but only as charts — [posix-spawn README](https://github.com/rtomayko/posix-spawn/)

### Inferences
- Fast Kernel Headers measures, from outside the compiler, the upper bound on what "don't process unneeded header content" is worth: about 40% less CPU on a stock x86 config. A compiler can capture part of this without editing headers. Lazy handling of unused inline bodies and declarations (next section) targets exactly the content Molnar removed by hand.
- Make's parse and stat overhead (seconds on a no-op build) has to be measured directly in vroom. With make kept (a user constraint), the levers are fewer make invocations, cheap shims (no fork from a large parent), and cheap `.cmd`/fixdep processing.

### Gaps
- No current, authoritative numbers for defconfig, tinyconfig or allnoconfig build times, or for macOS posix_spawn latency in µs; vroom should measure these locally. The LPC 2020 PDF with the per-phase breakdown could not be text-extracted. It is saved at /Users/johandahlin/.claude/projects/-Users-johandahlin-dev-vroom--claude-worktrees-assembler-31acee/3638328d-28b8-4f7d-8770-4bd5c265638a/tool-results/webfetch-1794139287729-7eb5kt.pdf
- Whether Fast Kernel Headers was merged upstream (it was largely not merged as a whole; I did not verify which parts were).
- "Kernel build in seconds" records: not found.

## Query-based / demand-driven compilation and lazy work

### Takeaway
Mainstream C compilers already avoid *codegen* for unused static and inline functions: clang defers them and emits only those referenced. They still fully parse and semantically check every header body. Making the front end lazy (skipping bodies, analysing on demand) is the unexploited part. The only published kernel-level evidence of its value is indirect: Fast Kernel Headers' uninlining and header splitting.

### Cited Findings
- Clang CodeGen defers `static` and `inline` definitions: it keeps them in a deferred-decls map keyed by mangled name, moves them to an emission queue on first use, and iterates until no new uses appear — [clang commit history (CodeGenModule)](https://cdn05042023.gitlink.org.cn/replica/hanchenye-llvm-project/commit/45470943a9bcaa55e859db2e7236897412cfc0dd)
- Kernel headers contain many unused `static inline` functions per TU. Clang warns about them (-Wunused-function) and GCC deliberately does not, so the kernel added `__attribute__((unused))` to its `inline` definition (2017) — [LKML 2017](https://lkml.iu.edu/hypermail/linux/kernel/1705.3/01639.html)
- rustc's query system is demand-driven: work runs only when a query is requested, results are memoized and fingerprinted, and red-green early cutoff applies — [rustc-dev-guide](https://rustc-dev-guide.rust-lang.org/queries/incremental-compilation.html)
- VisualAge C++ recompiled function by function from the CodeStore — [comp.compilers 2000](https://compilers.iecc.com/comparch/article/00-05-050)

### Inferences
- In vcc, the cheapest lazy step is to lex and brace-match `static inline` bodies from headers but parse and check them only when referenced (or when address-taken in an initializer). Diagnostics parity with clang may need a fallback, since clang does check every body.
- Combining this with a build-wide cache of header bodies (by token range plus macro-state key) means each unused body costs a skip once per process, and each used body is parsed once per key rather than once per TU.

### Gaps
- No source quantified the fraction of kernel header inline functions unused per TU; vroom should measure it, e.g. with vcc counting definitions vs references per TU.
- I found no production C compiler that skips sema of unused static inline bodies. TCC and Zig's lazy analysis (Zig analyses only reachable declarations) were not checked with sources in this pass.
- Clang's `-fdelayed-template-parsing` and lazy body parsing in IDE tooling (clangd preamble, skip-function-bodies) were not researched.
