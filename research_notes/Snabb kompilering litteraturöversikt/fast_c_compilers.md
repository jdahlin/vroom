# Fast C compiler front ends and single-pass compilers: compile-time techniques and numbers

Scope: compile *time*, not output quality. Context: vroom/vcc (SIMD lexer, token cache, interning, clang-exact pp, GNU C11 sema, emitter, as, ld, persistent vccd server), target: cold arm64 kernel build < 2 s on M1 Pro.
Research done November 2026; about 20 tool calls, so several sub-questions are only partly covered (see Gaps).

## 1. tcc (TinyCC): architecture, speed, TCCBOOT

### Takeaway
tcc is the reference point for "fast C". It is one ~100 KB binary with preprocessor, compiler, assembler and linker, and it emits machine code directly with no IR or textual asm. Its published figure is old: ~9x faster than gcc -O0 and ~859k lines/s (including headers) on a 2.4 GHz Pentium 4. TCCBOOT (2004) compiled and booted a Linux 2.4-era kernel in under ~15 s on that machine, but needed changes to the kernel build.

### Cited Findings
- Benchmark on the tcc homepage (Links browser): 76,936 source lines, which become 1,950,947 lines (67.2 MB) once headers are counted. **tcc 0.9.22: 2.27 s = 859,000 lines/s, 29.6 MB/s. GCC 3.2 -O0: 20.0 s = 98,000 lines/s, 3.4 MB/s.** That is about 9x. Wall time includes compile, assemble and link. Machine is a 2.4 GHz Pentium 4. The page gives no year, but the tcc 0.9.22 / gcc 3.2 era is about 2004–2005 — [bellard.org/tcc](https://bellard.org/tcc/)
- Architecture claims: "generates x86 code. No byte code overhead"; full C preprocessor and a GNU-like assembler built in; ~100 KB executable holding the preprocessor, compiler, assembler and linker; usable as a JIT library (libtcc) — [bellard.org/tcc](https://bellard.org/tcc/)
- TCCBOOT (news item dated 27 Oct 2004): a boot loader that compiles "a typical Linux kernel" from source and boots it in under 15 s on a 2.4 GHz P4. It needs 64 MB RAM — [bellard.org/tcc/tccboot.html](https://bellard.org/tcc/tccboot.html)
- The kernel was Linux 2.4.26. A later tcc fork commit records a change that "breaks compilation of the tccboot (linux 2.4.26 kernel)" — [PMSF TinyCC commit](https://gitea.pmsf.net/OSO/TinyCC/src/commit/2bf43b5483fb65b19b6277948d6438d7c4ccc3bc)
- Wikipedia (mirror) says TCCBOOT boots in ~10 s and needed changes to the Linux build process — [Wikipedia mirror](https://ipfs.ssi.eecc.de/ipns/en.wikipedia-on-ipfs.org/wiki/Tiny_C_Compiler). This conflicts with the "<15 s" on the primary page; prefer the primary.
- x86-64 support arrived only in tcc 0.9.25 (Shinichiro Hamaji) — [tcc changelog](https://bellard.org/tcc/changelog.html)
- A third-party benchmark repo says tcc's build speed is "vastly superior" because of its single-pass code-generation design — [nordlow/compiler-benchmark](https://github.com/nordlow/compiler-benchmark)
- Code-quality cost: tcc and chibicc "only hardcode a few registers and work more like stack machines" (informal Reddit comment) — [r/Compilers](https://libreddit.winscloud.net/r/Compilers/comments/1eplgbk/is_qbe_70_of_llvm)

### Inferences
- 29.6 MB/s on a 2004 P4 means the whole pipeline (pp, parse, codegen, asm, link) costs about 80 cycles per input byte. A modern M1 P-core (roughly 3–5x IPC×clock) should reach about 100–150 MB/s per core with a tcc-style design. The kernel's arm64 defconfig is several GB of preprocessed text in total (~1 GB/s across 8 cores is needed for 2 s), so even tcc-class speed is borderline. Header-volume reduction or reuse (token cache, server-side caching) is required, not optional.
- tcc's key speed properties are a single binary (no fork/exec of cc1/as/ld), no IR, no textual assembly, and direct machine code into in-memory sections. vcc's "emitter → assembler" split costs time if the boundary is textual. Binary/in-memory handoff is the tcc lesson.
- TCCBOOT targeted 2.4. No source found for tcc building a modern (5.x/6.x) kernel. Modern kernels rely on many GNU extensions (asm goto, `__builtin_*`, attributes, static_call, etc.) that tcc doesn't support. vcc's clang-exact approach is the opposite trade-off.

### Gaps
- No modern (2015+) primary measurement of tcc lines/s on current hardware was found. The homepage links "up-to-date tests" dated 2013, but they were not fetched.
- No source found for any attempt to build a 5.x/6.x kernel with tcc, or for the exact patch set TCCBOOT needed.
- Internal tcc speed techniques (hash-table token lookup, value stack `vtop` codegen, no AST) are well known from the source code but were not sourced from a document here.

## 2. Other small/fast compilers: Plan 9 kencc, Go, cproc/QBE, MIR c2m, chibicc, Aro, etc.

### Takeaway
Plan 9 (1990) shows the "fast compile, slow load" split and a header discipline (headers may not `#include`, so each header is read exactly once). Go turned that discipline into language rules: one file per import, export data at the head of the object file, unused imports are an error. Go's authors call this dependency design "the single biggest reason" Go compiles faster than C/C++. Few hard compile-speed numbers exist for cproc, chibicc, kefir, lacc, 8cc or Aro.

### Cited Findings
- **Plan 9 C compilers (Thompson, Summer 1990 UKUUG):** each target has its own compiler, assembler and loader (e.g. vc/va/vl for MIPS). The compiler does preprocessing, lexing, parsing, codegen, local optimisation and "the first half of assembly". It emits a compact binary object format, not text. The code generator is recursive descent ordered by Sethi-Ullman number, under 500 lines. The loader does the second half of assembly (branch resolution, delay slots, data layout) and linking — [Plan 9 C compilers](https://9p.io/sys/doc/compiler.html)
- Plan 9's built-in preprocessor handles `#ifdef`/`#include` but **not `#if`** (an external ANSI cpp is used if needed). `#pragma lib` in headers tells the loader which libraries to link — [Plan 9 C compilers](https://9p.io/sys/doc/compiler.html)
- Plan 9 numbers (MIPS R4000, 26 files / 12,600 lines): **vc -N 4.49 s compile + 1.72 s load; vendor cc 14.83 s compile; vendor cc -O 43.59 s.** On a 33 MHz 486, Plan 9 took 27 s and Watcom 31 s. The paper's own summary: fast to compile, slow to load, medium code quality. It notes that the serial loader hurts multiprocessor wall time — [Plan 9 C compilers](https://9p.io/sys/doc/compiler.html)
- **Pike, "Go at Google" (SPLASH 2012):** in 1984 a compile of `ps.c` processed `<sys/stat.h>` 37 times. In 2007 a Google C++ binary of ~2000 files and 4.2 MB grew to **>8 GB of compiler input after includes, a 2000x blow-up**. — [go.dev/talks/2012/splash.article](https://go.dev/talks/2012/splash.article)
- Same source: in Plan 9, headers could not contain `#include`, so "each #include file was read exactly once". This "required some discipline" and gave "much faster compilation". — [Go at Google](https://go.dev/talks/2012/splash.article)
- Same source, Go design: an import opens exactly one file (the dependency's object file). Export data comes first in the object file, so the reader can stop early. Unused imports are a compile **error**. Cyclic imports are banned. Measured fan-out for a large Go program was about 40x, "fifty times better than C++". The authors call this "the single biggest reason why Go compilations are faster than C or C++ compilations." — [Go at Google](https://go.dev/talks/2012/splash.article)
- **MIR c2m (Makarov, Red Hat blog, Apr 2021, measured Nov 2020 on an i9-10900):** the front end has four passes of roughly equal size (pp, parser, context checker, MIR generator). The parser is hand-written PEG with rare backtracking, "a bit slower than deterministic parsers". The stated goal was "simplicity, not compilation speed". tcc's speed is called "incredible". gcc and clang are the "worst performers" on startup time. `-p` compiles files in parallel threads. Numbers appear only as a relative log-scale chart (bzip2 as one ~6,500-line file) — [Red Hat blog](https://developers.redhat.com/blog/2021/04/27/the-mir-c-interpreter-and-just-in-time-jit-compiler)
- Makarov: "c2m compilation speed is 1.5 times faster than cproc one" (no machine or year given) — [MIR discussion #273](https://github.com/vnmakarov/mir/discussions/273)
- cproc/QBE: one informal comment says it "seems to truly compile very fast". Its runtime code reaches 74–82% of gcc on oksh benchmarks (runtime, not compile time) — [MIR discussion #273](https://github.com/vnmakarov/mir/discussions/273); [Callahan blog 2021](https://briancallahan.net/blog/20211010.html)
- **Aro (Zig's C front end, Vexu/arocc):** stated goal is "fast compilation and low memory usage with good diagnostics". It ships in Zig as an alternative translate-c front end. The plan is for it to eventually replace clang-based translate-c and to compile C by translating it to Zig. No benchmark numbers were published — [arocc README (Zig mirror)](https://git.jakstys.lt/motiejus/zig/raw/lib/compiler/aro/README.md); [Vexu/arocc](https://github.com/Vexu/arocc)

### Inferences
- The Plan 9/Go lesson is that the biggest compile-time win is reading less input, not lexing faster. vcc can't change kernel headers, but a persistent server (vccd) can emulate "each header read once". The approach: cache the lexed/preprocessed token streams of headers keyed on (path, macro state that affects them), as clang's PCH/modules do at a coarser level. vcc's token cache already points this way. Extending it to cached *parsed declarations* for common header prefixes (compiler_types.h, kconfig.h, linux/kernel.h ...) is the analogue of Go export data.
- The Plan 9 "split assembly across compiler and loader, binary object format" design matches vcc's emitter/vas/vld. The paper warns that a serial loader limits multicore wall time, which matters for the final vmlinux link. Keep vld parallel and light.
- MIR c2m's four roughly equal passes suggest that an un-tuned front end has no single dominant phase. Speed comes from cutting allocations and passes, not from optimising one hotspot.

### Gaps
- No compile-speed numbers found for chibicc, kefir, lacc, 8cc, cparser/libfirm or Aro. The nordlow/compiler-benchmark repo may have cproc/tcc/gcc numbers, but they were not extracted.
- Turbo C / Borland history (single-pass, in-memory compile, Turbo Pascal lineage) was not researched. No primary numbers.
- Thompson's earlier "A New C Compiler" (1990) was not fetched separately. The Plan 9 compiler doc above is the related primary source.
- Go's current export data format (unified IR, indexed export) and its numbers were not fetched.

## 3. Clang and GCC compile-time engineering; -O0 back-end speed (FastISel, GlobalISel, TPDE)

### Takeaway
Clang's early speed claims (2007) put its preprocessor ~40% faster and its parser+AST ~4x faster than GCC's, with 5x less AST memory. These come from mirrored copies of an old features page, so check them before relying on them. At -O0, the LLVM back end is the main target of recent work. TPDE (2025) compiles LLVM IR 8–24x faster than LLVM -O0, yet gives only ~17% end-to-end gain in clang. The back end is only ~15% of total clang time in that setup, so for C the **front end (pp+parse+sema+IRgen) dominates at -O0**.

### Cited Findings
- Clang features page (2007-era, Apple opensource mirror): compiling Carbon.h (558 files transitively, 12.3 MB) was ~2.5x faster than GCC. A later version of the page says the preprocessor is "consistently 40% faster than GCC's" and the parser + AST builder "~4x faster", with ASTs using 5x less memory. It also says a faster preprocessor makes distcc 3x more scalable — [Apple opensource mirror of clang features.html](https://opensource.apple.com/source/clang/clang-163.7.1/src/tools/clang/www/features.html.auto.html) (via search snippet; mirror, not original slides). The current clang features page drops these numbers and only says "fast, light and scalable" and that the library design makes per-layer profiling easy — [clang.llvm.org/features.html](https://clang.llvm.org/features.html)
- 2007 original clang README: clang was significantly faster than GCC at semantic analysis, parsing, preprocessing and lexing — [2007-OriginalClangReadme](https://android.googlesource.com/toolchain/llvm/+/bb8d9165a419a4281d34391148296a069fab5fe1/docs/HistoricalNotes/2007-OriginalClangReadme.txt)
- **TPDE (Schwarz, Kamm, Engelke, TUM; arXiv 2505.22610, May 2025):** back end for LLVM IR targeting x86-64 and AArch64. On SPECint 2017 at -O0 it is **8–24x faster than LLVM -O0** (geomean 12.15x on x86-64, 18.96x on AArch64), with -O0-level runtime performance. — [arXiv abstract](https://arxiv.org/abs/2505.22610); [arXiv HTML](https://arxiv.org/html/2505.22610)
- TPDE end-to-end inside clang: back-end share falls **from 15% to 2%** of compile time, for an **average end-to-end speedup of 17%** — [arXiv HTML](https://arxiv.org/html/2505.22610)
- The larger AArch64 gap exists because LLVM defaults to **GlobalISel on AArch64 -O0, "significantly slower than FastISel"** — [arXiv HTML](https://arxiv.org/html/2505.22610)
- TPDE techniques:
  - one analysis pass: linear-time liveness (Kohn et al.), loop detection (Wei et al., handles irreducible loops), RPO block layout with each loop's blocks kept together
  - then **a single pass that combines instruction selection, register allocation and encoding**
  - greedy local register allocation: lowest free register first, round-robin eviction to spill, fixed callee-saved registers for loop-carried values, and no later rewriting of emitted code
  - instruction "snippet" encoders generated ahead of time from C/C++ compiled through LLVM's MIR
  — [arXiv HTML](https://arxiv.org/html/2505.22610)
- TPDE vs Cranelift on Wasm: 4.27x faster than default Cranelift, 2.68x faster than Cranelift with its fast register allocator — [arXiv HTML](https://arxiv.org/html/2505.22610)
- Engelke, EuroLLVM 2024 "A Wish List for Faster LLVM Back-ends": a profile of the -O0 x86-64 back end shows ~3% in `MachineInstr::addOperand`, ~1% each freeing LLVM IR and MIR, and ~0.3% of compile time per pass just iterating over IR. Death by many small costs — [slides](https://www-new.llvm.org/devmtg/2024-04/slides/QuickTalks/Engelke-AWishList-ForFasterLLVM-Backends.pdf) (via search snippet)
- FastISel exists because at -O0 the optimisers don't run, so codegen takes a much larger share. It skips SelectionDAG for most code — [FastISel.cpp](https://llvm.org/docs/doxygen/FastISel_8cpp_source.html)
- Lattner (llvm-dev, Nov 2009): at -O0 the front end can cut codegen cost by not emitting inefficient IR, since no optimiser runs to clean it up — [llvm-dev 2009-11](https://lists.llvm.org/pipermail/llvm-dev/2009-November/027393.html)
- Sony (around 2016, older LLVM): median back-end share is ~52% in debug builds vs ~81% in release (mostly C++ game code) — [Sony prepo wiki](https://github.com/sonyinteractive/sn-llvm-project-prepo/wiki/Time-Spent-in-Code-Gen)
- Informal (HN, 2025): at -O0, 50–80% of clang time is front end — [HN](https://hn.svelte.dev/item/45072481) (opinion, not measured)

### Inferences
- For vcc, the TPDE result means a fast single-pass back end (which vcc's emitter is meant to be) only pays off once the front end is also fast. In clang -O0, about 85% of time is outside the back end.
- TPDE's design is a good template for vcc's arm64 emitter: one analysis pass, then one combined isel+regalloc+encode pass, greedy local regalloc, pre-generated encoders, and direct binary encoding without textual asm.
- Sources disagree on back-end share (15% for TPDE/SPEC C, ~52% for Sony debug C++). The split depends heavily on the workload. Kernel TUs are header-heavy with little code per TU, which suggests an even smaller back-end share. That is inference, not measured.

### Gaps
- nikic's llvm-compile-time-tracker findings (e.g. opaque pointers, per-release regressions in instructions retired) were not fetched with numbers. His 2022 year-in-review mentions opaque pointers lowering compile time and memory — [nikic 2022](https://nikic.github.io/2022/12/20/This-year-in-LLVM-2022.html).
- Clang's specific lexer/preprocessor fast paths were not sourced here: the multiple-include (header guard) optimisation, `IdentifierInfo` with a cached pp-keyword/macro bit, `SourceManager` offset encoding, `HeaderSearch` lookup caching, `#pragma once`. These are known from clang's Internals Manual and source but not cited in these notes.
- GCC compile-time work (e.g. `-ftime-report`, the 2010 "speed of the compiler" thread [gcc-patches 2010-08](https://gcc.gnu.org/pipermail/gcc-patches/2010-August/291395.html)) is not summarised. The thread was found but not read.
- No talks by Chandler Carruth or Aaron Ballman on clang compile time were found in this pass.

## 4. sparse (Linux semantic checker) speed

### Takeaway
No benchmark numbers found. The kernel docs describe `make C=2` (sparse on all files) as a fast way to check the whole tree after a build, but give no timings.

### Cited Findings
- `make C=1` runs sparse on the C files being recompiled; `make C=2` runs it on all files. The `CF=` variable passes options. Sparse is described as a semantic checker for finding problems in kernel code — [kernel sparse.rst](https://grok.openbmc.org/xref/openbmc/linux/Documentation/dev-tools/sparse.rst?r=93032e31)
- The sources disagree on endianness checking: one says `__CHECK_ENDIAN__` is off by default, another says it has been on since v4.10-rc1 — [kernel sparse.rst](https://grok.openbmc.org/xref/openbmc/linux/Documentation/dev-tools/sparse.rst?r=93032e31); [wireless wiki](https://wireless.wiki.kernel.org/en/developers/documentation/using-sparse)

### Inferences
- sparse is a full kernel-capable C front end (pp+parse+typecheck, with its own linearised IR) that handles the kernel's GNU C. Its throughput would be a useful baseline for "front end only" on kernel TUs. Measuring it locally (`make C=2` on work/build-o0 vs `vpp -fsyntax-only`) is cheap.

### Gaps
- No published sparse throughput numbers. A local measurement is needed.

## 5. Where time goes in a C compile of a kernel TU

### Takeaway
No published per-phase profile of a kernel TU was found. Kernel developers at Google (2023) say kernel builds are "held back by lexing and parsing needless tokens". One data point: `lib/string.o` preprocessed to 23,941 lines with a 0.36 s build. After an IWYU cleanup it was 5,092 lines (-78%) with a 0.12 s build (-67%). Build time scales roughly with preprocessed volume.

### Cited Findings
- Hasan & Desaulniers (Google, LPC / Kernel Exchange 2023H2): "Build times are held back by lexing and parsing needless tokens"; "many files become hundreds of times longer when preprocessed"; "Compiler frontend does not sufficiently address preprocessing bloat" — [LPC 2023 slides](https://lpc.events/event/17/contributions/1620/attachments/1228/2520/Linux%20Kernel%20Header%20Optimization.pdf)
- Same source: x86 defconfig `lib/string.o` went from **23,941 to 5,092 preprocessed lines (-78%)** and **0.36 s to 0.12 s** build time. The binary was unchanged apart from one `__LINE__` in a WARN_ON — [LPC 2023 slides](https://lpc.events/event/17/contributions/1620/attachments/1228/2520/Linux%20Kernel%20Header%20Optimization.pdf)
- Same source: on a 128-core machine an x86 defconfig build took ~72.3 s. After IWYU changes to 220 of ~2,700 files it took ~69.0 s, with >1 million lines removed. PCH candidates (force-included in every TU): `compiler_types.h`, `kconfig.h`, `compiler-version.h` — [LPC 2023 slides](https://lpc.events/event/17/contributions/1620/attachments/1228/2520/Linux%20Kernel%20Header%20Optimization.pdf)
- General tooling: clang `-ftime-trace` (clang 9+) with ClangBuildAnalyzer splits front end ("Parsing") from back end ("Codegen & opts") and ranks the most expensive headers. A sample of 1,761 compilations (C++) showed parsing 5,167 s vs codegen 7,577 s — [ClangBuildAnalyzer](https://github.com/aras-p/ClangBuildAnalyzer). In a pbrt (C++) study, one file's time was split about evenly between header parsing and codegen — [pharr.org 2019](https://pharr.org/matt/blog/2019/10/23/profiling-the-pbrt-build)

### Inferences
- In string.o, 78% fewer lines gave 67% less time. That implies at least ~70–80% of a small kernel TU's compile time scales with header volume (pp+lex+parse+sema of declarations never used). Codegen for the TU's own functions is a minority. For vcc this means header processing is the main cost: token cache, lazy sema of unused declarations, skipping bodies of unused `static inline` functions, and server-side reuse of header state.
- A ~3,000-TU arm64 defconfig with ~20–30k preprocessed lines per TU means tens of millions of lines per build, mostly the same headers. Reuse across TUs inside vccd is the lever with the largest headroom. It is the runtime analogue of Plan 9's "read each header once".
- A local clang `-ftime-trace` run over work/build-o0 (via KCFLAGS) would give the missing kernel-specific front-end/back-end split cheaply.

### Gaps
- No published `-ftime-trace` or `-ftime-report` breakdown of a Linux kernel build found.

## 6. Header-heavy cost: Ingo Molnar's "Fast Kernel Headers" (2022), IWYU, build-time trends

### Takeaway
Molnar's 2,297-patch fast-headers tree (Jan 2022, based on v5.16-rc7) cut x86 `make -j96 vmlinux` wall time from 231 s to 130 s (+78% throughput) and CPU time by 38% (+61.6%). It did this purely by untangling headers so each TU parses less. On ARM64 the wall-time gain was only 18–35% because the final vmlinux link serialises the build. The series was never merged as a whole as of the sources read.

### Cited Findings
- v1 announcement, quoted on LKML: `make -j96 vmlinux`, stock x86 distro config with modules built in. **v5.16-rc7: 231.34 ± 0.60 s; fast-headers-v1: 129.97 ± 0.51 s (+78.0%). CPU: 11,474,982 ms → 7,100,730 ms (+61.6%).** Covers ~10,000 headers, 25+ subtrees, >2,200 commits. Uses `CONFIG_FAST_HEADERS=y` and `CONFIG_KALLSYMS_FAST=y` — [LKML reply quoting v1](https://lkml.iu.edu/hypermail/linux/kernel/2201.0/00490.html); [patchew v1](https://patchew.org/linux/YdIfz+LMewetSaEB@gmail.com/)
- Technique: decouple `<linux/sched.h>` (`struct task_struct`) through a new `per_task()` mechanism modeled on per-CPU, where fields live in `task_struct->per_task_area[]` at offsets produced by asm-offsets. Ingo claims no runtime cost. Greg KH raised concerns about maintaining the generated struct definitions — [LKML](https://lkml.iu.edu/hypermail/linux/kernel/2201.0/00490.html)
- Incremental rebuilds: touching `include/linux/sched.h` went from 230.30 s to 108.35 s (+112%); `include/net/sock.h` +159%. On ARM64, wall-time improvement was "only" 18–35% because vmlinux linking serialises the build, while CPU time improved ~51% — [cnx-software summary](https://www.cnx-software.com/2022/01/04/the-linux-kernel-could-soon-be-50-to-80-faster-to-build/); [LKML thread](https://lkml.iu.edu/hypermail/linux/kernel/2201.1/00090.html) (secondary summaries of Molnar's numbers)
- v2 (8 Jan 2022): Clang build went from 337.8 s to 179.8 s (+88%). `<linux/sched.h>` is now included in only ~36% of .c files, versus 99% in mainline — [v2 announcement](https://lkml.iu.edu/hypermail/linux/kernel/2201.1/00167.html) (via search summary)
- A v3 was posted in March 2022. Google's 2023 slides say "unclear what the status of this series is" and pursue automated IWYU instead — [LPC 2023 slides](https://lpc.events/event/17/contributions/1620/attachments/1228/2520/Linux%20Kernel%20Header%20Optimization.pdf)
- IWYU on the kernel hits several problems:
  - no stdint.h, so its built-in symbol tables must be disabled
  - asm/asm-generic need private mappings
  - it can't see macro uses and strips X-macro headers
  - token pasting makes analysis hard
  — [LPC 2023 slides](https://lpc.events/event/17/contributions/1620/attachments/1228/2520/Linux%20Kernel%20Header%20Optimization.pdf)

### Inferences
- CPU time fell by 38% just by parsing fewer header tokens. In a gcc/clang kernel build, roughly 40%+ of CPU time is therefore spent on header content that the TU doesn't need. That is the same budget vcc can attack *without* changing the source, through caching, laziness and reuse in vccd.
- The ARM64 finding (link serialisation caps wall time at 18–35%) is directly relevant to vroom's arm64 target. At a < 2 s budget the serial vmlinux link (plus kallsyms, BTF and similar passes) can dominate wall time. The linker and the post-link steps must be parallel, or overlapped with compilation.
- vroom builds an unmodified kernel, so source-level fixes like Molnar's are out of scope. The numbers serve as an upper bound on what "seeing less header" is worth: ~40% of CPU.

### Gaps
- Long-term Linux build-time trends (e.g. growth of preprocessed lines per TU across versions) were not found with numbers.
- Whether parts of fast-headers were merged upstream piecemeal (2022–2026) was not confirmed.
