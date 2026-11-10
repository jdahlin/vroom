# Compiler-internal architecture and low-level techniques for compile speed (DoD, memory, SIMD, hashing, parallelism)

Scope note: about 20 search/fetch calls, November 2026. Where a number comes from a secondary source or a forum, it is marked as such. Applicability to vroom/vcc (M1 Pro, NEON, 16 KB pages, persistent vccd) is given under Inferences.

## Data-oriented compiler design (Zig, Carbon, Roc, Jai, others)

### Takeaway
Measured wins from struct-of-arrays and 32-bit index layouts are real but moderate: about 11-26% in time and about 12-19% in memory on parsers that were already reasonable. Pointer- or Box-based trees cost much more: about 1.7x slower than arena or index layouts in the Roc measurement. The "millions of lines/s" figures for Carbon and Jai are goals or second-hand claims, not published benchmarks.

### Cited Findings
- **Zig PR #7920 "rework AST memory layout"** (Andrew Kelley, opened 31 Jan 2021, merged 25 Feb 2021).
  - Tokens are stored as SoA: a 1-byte tag and a 4-byte byte offset, so 5 B/token. There is no end offset; a token is re-tokenized when its end is needed.
  - AST nodes are SoA: a 1-byte tag, a 4-byte `main_token` index and two u32 slots (`lhs`/`rhs`), so 13 B/node with no padding. Nodes with 3 or more children put an index into a shared `extra_data` u32 array. Separate tags encode common shapes, for example a call with one argument versus many. [GitHub PR #7920](https://github.com/ziglang/zig/pull/7920)
  - Preliminary parse-only results: 15% fewer cache misses, 28% fewer instructions, 26% fewer cycles, 22% faster wall clock. [GitHub PR #7920](https://github.com/ziglang/zig/pull/7920)
  - `zig fmt` over the whole standard library: peak memory 84.7 MiB to 68.4 MiB (-19.3%), instructions 4.59G to 3.94G (-14.2%), cycles 1.70G to 1.50G (-11.8%), wall clock 0.390 s to 0.345 s (-11.4%). Cache misses went up 34.5%, from 2.80M to 3.77M. [GitHub PR #7920](https://github.com/ziglang/zig/pull/7920)
- An earlier 2020 Zig commit had smaller wins: full std-lib AST in memory 93.9 to 88.5 MiB, `zig fmt` peak 74.0 to 73.0 MiB, and instruction count slightly up. This shows that only the full SoA and index rework paid off. [search summary of Zig commit history](https://git.jakstys.lt/motiejus/zig/src/commit/4dca99d3f6b732c415d270f0c97def144ed6d3b7)
- Mitchell Hashimoto's walk-through of Zig internals says the parser uses DoD for memory use and cache locality, and that the same pattern recurs in later stages (ZIR, AIR). [mitchellh.com](https://mitchellh.com/zig/parser)
- **Carbon toolchain design doc.**
  - The phases are distinct and each one materializes a full output structure: SourceBuffer, Lex::TokenizedBuffer, Parse::Tree, SemIR::File, LLVM Module, object file. The doc avoids callbacks between phases and gives "memory locality" as a reason.
  - Data lives in vectors and is addressed by `int32_t` handles. For example, the parse tree is a `SmallVector<NodeImpl>` indexed by `Parse::Node`.
  - The parser is iterative: it keeps an explicit `state_stack_` of `Parse::State` and does not recurse.
  - Parsing is deliberately permissive and pushes some errors to Check, so that parsing stays lighter.
  - The doc gives no numeric targets; it lists compile performance as one of four priorities. [Carbon toolchain docs](https://docs.carbon-lang.dev/toolchain/docs/)
- **Carbon speed targets.** "Lex+parse 10M LOC/s, semantic analysis 1M LOC/s" (attributed to Chandler Carruth) appears only in a 2024 Reddit comment. Another commenter there says no numbers are in the design docs. Treat it as unverified. [Reddit r/ProgrammingLanguages mirror](https://redlib.hbubli.cc/r/ProgrammingLanguages/comments/1co8qpv/flat_ast_and_states_machine_over_recursion_is)
- **Roc parser DoD case study** (Tweede Golf, 15 Mar 2023).
  - Splitting `Defs` from AoS into SoA (separate `regions`, `space_before`, `space_after`): memory 75.5 MB to 67.1 MB (about 11%), runtime 26.6 ms to 23.6 ms (1.12x).
  - Using `Box<Def>` instead of arena references: 399,862 allocations instead of about 46-99, and runtime 25.1 ms to 42.5 ms (1.69x slower). [Tweede Golf](https://tweedegolf.nl/en/blog/88/data-oriented-design/)
- **TU Delft student project** (Tea compiler, not Zig) applying SoA to the AST: 3.5x-7.1x faster type checking, 2.3x-3.1x faster codegen, and an AST 6-12x smaller. This is a poster-level result. [TU Delft poster](https://cse3000-research-project.github.io/static/a3f06b5f770d180ce977c00e14315180/poster.pdf); [TU Delft repository](https://repository.tudelft.nl/file/File_5748302e-41df-4214-b489-32eca09e39bf)
- **Jai.** The numbers are second-hand reports of Blow's talks: an 80k-line game in about 1.5 s on a laptop and under 1 s on a desktop, and a goal of 1M lines/s. Jai is not public, so none of this can be verified. [digitalmars forum, 2018](https://forum.dlang.org/post/pxolyztqegrmontctbhe@forum.dlang.org); [puremagic list](https://lists.puremagic.com/pipermail/digitalmars-d/2018-September/285538.html); [hacklines/Cone](https://hacklines.com/en/editions/99907/click)
- **TCC, as a baseline for a C toolchain.**
  - Bellard's page reports that TCC 0.9.22 compiled the Links browser (about 77k lines) at 859,000 lines/s on hardware from about 2004. TCC compiles, assembles and links in one pass. [bellard.org/tcc](https://bellard.org/tcc/)
  - nordlow/compiler-benchmark finds tcc the fastest by a wide margin, followed by Cuik, Vox and dmd, and credits tcc's single-pass codegen. [nordlow/compiler-benchmark](https://github.com/nordlow/compiler-benchmark)

### Inferences
- For vcc, the Zig layout maps directly onto C: a token is `u8 kind` + `u32 offset` in parallel arrays, and a node is `u8 tag` + `u32 main_tok` + `u32 lhs, rhs` with an `extra[]` array. It is best to split hot fields (kind) from cold ones (spelling, location) so that parser loops only touch about 1 B/token.
- The rise in cache misses in the Zig `zig fmt` result, despite lower total time, shows that instruction count and memory footprint mattered more than misses there. Measure cycles and instructions, not only misses.
- Carbon's "each phase emits a complete array" model fits a C front end that runs after preprocessing. It conflicts with C's typedef feedback from sema into the parser, so a purely separate parse phase needs a typedef-aware lexer-hack interface.
- The Roc result (Box is 1.69x slower than arena) is the strongest argument in the material for never using per-node malloc.

### Gaps
- No primary-source numbers were found for Andrew Kelley's Handmade Seattle 2021 talk, "Practical Data-Oriented Design". The "39%" figure circulating comes from unverified lecture notes ([coconote](https://coconote.app/notes/148a6c66-d149-474f-9d34-d537952f2d4a)).
- Not researched for lack of budget: Odin, V, Hare, rust-analyzer/rowan, Roc's later Zig rewrite, and Carruth's talks with concrete numbers.

## Lexing and parsing: SIMD scanning, NEON tricks, keyword hashing

### Takeaway
simdjson-style two-stage scanning costs about 0.5-1 cycle/byte for structural indexing. On NEON the missing `movemask` is best replaced with `vshrn_n_u16(cmp, 4)`, which gives a 64-bit mask with 4 bits per byte, together with `rbit`+`clz`, or `ctz` directly on the 64-bit value. Nibble lookups with `vqtbl1q_u8` give character-class classification at more than 5 instructions/cycle on Apple cores.

### Cited Findings
- **simdjson** (Langdale and Lemire, VLDB Journal 28(6), 2019). Stage 1, structural indexing without UTF-8 validation, costs 0.5-1 cycles/input byte, and more on documents dense in structural characters. About half of total cycles are in stage 1, with 0.5-3 cycles/byte in total. Fitted model on Skylake: stage-1 cycles = 1.7·S + 0.62·B, where S is the number of structurals and B the number of bytes. [arXiv 1902.08318](https://arxiv.org/pdf/1902.08318)
- **NEON replacement for movemask** (Danila Kutenin, Google, Arm Community blog, 29 Aug 2022).
  - Sequence: `vceqq_u8` (gives 0x00/0xFF per byte), then `vshrn_n_u16(...,4)`, which gives a 64-bit value with 4 bits per byte. Extract it with `vget_lane_u64`. Optionally AND with `0x8888888888888888` to keep one bit per byte. Iterate with `rbit`+`clz` and `x &= x-1`, then divide the bit position by 4.
  - The SSE2NEON and SIMDe movemask emulations need at least 4 instructions with at least 2 cycles of latency each. On x86, movemask is 1 cycle.
  - Measured: strlen/memchr in Arm's optimized-routines went 10-15% faster on a strlen distribution from SPEC CPU 2017 when an `addp` sequence was replaced by a single `shrn`. zstd levels 5-9 improved 3.5-4.5%. Abseil's Swiss map improved 3-8%, but with a 64-bit mask and `0x80..80`, not shrn.
  - On Neoverse N1, `shrn` only issues on pipeline V1, while `umaxp` can issue on V0 or V1. [Arm Community blog](https://developer.arm.com/community/arm-community-blogs/b/servers-and-cloud-computing-blog/posts/porting-x86-vector-bitmask-optimizations-to-arm-neon)
- Interleaved `LD4` loads can emulate 64-byte movemasks. They are slower for a single mask but faster when several masks are computed at once. [engineered.at](https://engineered.at/articles/use-interleaved-vectors-for-parsing-on-arm)
- **Identifier location on NEON** (Lemire, 4 Sep 2023, "Locating identifiers quickly (ARM NEON edition)"). Character-class classification uses `vqtbl1q_u8` on the low and high nibbles. On an M2 the SIMD code retires more than 5 instructions/cycle, while the scalar table approach is bound by load latency. Lemire calls Apple's NEON good but weaker than AVX-512. Source: a mirror of Lemire's blog, seen in a search snippet; the page itself returned 404 when fetched. [mirror](https://eklausmeier.neocities.org/lemire/blog/2023/09-04-locating-identifiers-quickly-arm-neon-edition)
- Vectorized classification in HTML scanning measured about 20x over conventional methods (Lemire, arXiv 2025). [arXiv 2503.01662](https://www.arxiv.org/pdf/2503.01662)
- Lemire showed arbitrary byte-to-byte maps on NEON with `vqtbl4q` (64-byte tables) back in 2019. On x86 a 16-byte table was the limit until AVX-512. [Lemire 2019 mirror](https://eklausmeier.neocities.org/lemire/blog/2019/07-23-arbitrary-byte-to-byte-maps-using-arm-neon)
- Dougall Johnson: integer-to-string conversion with NEON on an M1 Max takes 0.88 ns/value, which he claims beats Lemire's AVX-512 variant. This is an example that Apple cores have high NEON throughput, with 4 SIMD pipes. [dougallj](https://hn.jakechampion.name/reader/https://dougallj.wordpress.com/2022/04/01/converting-integers-to-fixed-width-strings-faster-with-neon-simd-on-the-apple-m1/)
- Carbon's parser is an explicit state machine with no recursion. [Carbon toolchain docs](https://docs.carbon-lang.dev/toolchain/docs/)

### Inferences
- If vcc's lexer emulates movemask with `addp` chains, switching to `shrn #4` is a cheap win. The 4-bits-per-byte mask works directly with `__builtin_ctzll(m) >> 2`.
- Precomputing a bitmap per 64-byte block (identifier characters, whitespace, `"`, `'`, `/`, `\n`, `#`) in a simdjson-like stage 1 gives about 0.5-1 c/B for the scan. The real cost then moves to stage 2 (token emission), as in simdjson.

### Gaps
- No primary benchmarks were found for perfect-hash keyword lookup versus a switch on length plus first character, or for table-driven versus handwritten C parsers.
- No M1-specific latency and throughput table for `shrn`, `tbl` and `umaxv` was retrieved. Dougall Johnson's M1 instruction tables would be the source.

## Memory: arenas, never-free, huge pages, mmap, layouts

### Takeaway
The strongest data point is that per-node heap allocation (Box) costs about 1.7x against an arena in Roc. Zig's index layout cut peak memory by 19%. For a short-lived compiler, arenas and never-free are standard; pages and bandwidth matter more than malloc speed.

### Cited Findings
- Roc: arena references beat `Box` with 25.1 ms against 42.5 ms, and 46-99 against about 400k allocations. [Tweede Golf](https://tweedegolf.nl/en/blog/88/data-oriented-design/)
- Zig: 32-bit indices and SoA cut peak memory 84.7 to 68.4 MiB, and binary size 3.37 to 3.17 MiB. [GitHub PR #7920](https://github.com/ziglang/zig/pull/7920)
- rustc: a fixed 8 KiB `FileEncoder` buffer gave sub-1% wins. A minimum codegen-unit (CGU) size cut peak memory by up to 19%. Overall, peak memory fell 10.53% on average between March and August 2023. [Nethercote, Aug 2023](https://nnethercote.github.io/2023/08/25/how-to-speed-up-the-rust-compiler-in-august-2023.html)
- Carbon uses vectors and handles instead of heap pointers to reduce allocation overhead. [Carbon toolchain docs](https://docs.carbon-lang.dev/toolchain/docs/)

### Inferences
- With 16 KB pages on macOS, the first-touch cost per page applies to 4x fewer pages than on 4 KB systems. Reusing warm arenas in vccd, rather than re-mmapping per TU, avoids page faults and zeroing entirely.

### Gaps
- No primary measurements were found for huge or superpages on macOS, mmap versus read for source files, or malloc versus arena on Apple Silicon. A separate search is needed, for example on mimalloc benchmarks and Apple's xzone malloc.

## Strings and symbols: interning, hash functions, hash tables, concurrent maps

### Takeaway
For short identifiers, latency per hash matters, not GB/s: rapidhash takes about 1.8 ns for 4-16 B keys on an M1 Pro. Swiss tables filter on 7-bit H2 bytes in 8- or 16-wide groups. mold shows that an insert-only, non-rehashing concurrent map, presized with HyperLogLog, beats generic concurrent maps by a lot.

### Cited Findings
- **rapidhash** (successor to wyhash), from its README via docs.rs. Average latency for 4/8/16-byte keys on an M1 Pro: 1.79 ns for rapidhash against 1.92 ns for xxh3. Peak throughput is 47 GB/s on an M1 Pro for 16 KB-2 MB inputs. It claims significant throughput gains over wyhash and foldhash, without an M1 figure. [docs.rs rapidhash](https://docs.rs/rapidhash/=4.5.1/); [lib.rs](https://lib.rs/crates/rapidhash)
- wyhash small-key speed: about 28-32 cycles per hash for 1-30 byte keys, on an unspecified CPU. [fasthash-sys wyhash doc](https://docs.rs/crate/fasthash-sys-fork/latest/source/src/smhasher/doc/wyhash.txt)
- rustc hashing: switching the incremental fingerprint hasher from SipHash-2-4 to SipHash-1-3 gave a 1.63% average wall-time reduction, and `#[inline]` on `Hash::hash` gave sub-1% wins. This shows hashing is a measurable cost even in a large compiler. [Nethercote, Aug 2023](https://nnethercote.github.io/2023/08/25/how-to-speed-up-the-rust-compiler-in-august-2023.html)
- **Swiss table** (Abseil). H1, the high 57 bits, picks the group; H2, the low 7 bits, is stored as a control byte. The expected number of false H2 matches is k/128.
  - Group width is 16 on SSE2. On ARM64 some implementations use 8-byte groups (a 64-bit NEON or SWAR mask), and Go uses 8-slot groups. [Abseil raw_hash_set.h](https://android.googlesource.com/platform/external/abseil-cpp/+/3ae63243/absl/container/internal/raw_hash_set.h); [golang.design](https://golang.design/under-the-hood/en/part2lang/ch05data/swisstable/); [swiss_hash gem](https://gemdocs.org/gems/swiss_hash/0.1.1/)
  - Abseil on ARM gained 3-8% from a NEON 64-bit mask with `0x8080808080808080`. [Arm Community blog](https://developer.arm.com/community/arm-community-blogs/b/servers-and-cloud-computing-blog/posts/porting-x86-vector-bitmask-optimizations-to-arm-neon)
- **mold.** String merging of `.debug_str` dominated link time, and TBB's concurrent hashmap scaled poorly. It was replaced with an insert-only map that never rehashes, sized beforehand with a HyperLogLog cardinality estimate. Linking clang-13 with debug info got about 30% faster, to about 2.5 s. [mold commit 41b2fa7](https://github.com/rui314/mold/commit/41b2fa73757b2e4eb44fc68e216a80d2a56af297)
- mold's design notes treat symbol resolution as one of the heaviest stages, and say it can be moved to a preload stage through string interning. [mold design notes (fork)](https://github.com/johejo/mold)

### Inferences
- For vcc's interner, the lexer has already loaded the identifier into a NEON register. A hash computed from those 16 bytes (for example CRC32 with `__crc32cd`, or a multiply-fold) plus a Swiss-style 8-wide H2 probe costs less than a separate hash pass. CRC32 on arm64 had no primary benchmark in this search.
- mold's insert-only, presized, non-rehashing map is directly applicable to a shared identifier table in vccd and to vld's symbol table.

### Gaps
- No primary sources were retrieved on FxHash in rustc (algorithm and measured wins), ahash, or CRC32C as an identifier hash on Apple cores.

## Parallelism within and across compilation units; process startup

### Takeaway
Intra-TU parallelism gives 1.3x-3.5x in mature compilers: rustc's front end averages 30% and reaches up to 50%, and GCC's `-fparallel-jobs` reaches about 1.9x on 4 cores and 3.5x on single files in the 2022 paper. Utilization is uneven, though, and for a kernel build with about 3,000 TUs, cross-TU parallelism on 10 cores already saturates the machine. Process startup on macOS is expensive and poorly documented, which is an argument for a persistent server.

### Cited Findings
- **rustc parallel front end** (Nethercote and the Parallel Rustc WG, Rust blog, 9 Nov 2023). It is intraprocess and enabled with `-Z threads=8`. Overhead with 1 thread is about 0-2% against the serial front end. Up to 50% shorter compile time on real code was reported by the press from the WG's measurements. Thread utilization is uneven, with idle periods. [Rust blog](https://blog.rust-lang.org/2023/11/09/parallel-rustc/); [InfoWorld](https://www.infoworld.com/article/3710332/rust-compiler-front-end-gains-parallel-execution.html)
- RustConf, October 2024: about a 30% aggregate improvement on nightly. Macro expansion is still single-threaded. [LWN](https://lwn.net/Articles/995125/)
- **Status in 2026.** The project goal page reports that a race in parallel incremental compilation has been fixed and that stabilization will start. No source confirms a stable release. [Rust project goals 2026](https://rust-lang.github.io/rust-project-goals/2026/parallel-front-end.html)
- rustc aggregate, March-August 2023: -7.13% average wall time across 513 benchmark results and -10.53% peak memory. These came from many small changes, typically 1-2% each, plus LLVM upgrades of 1.2-1.9%. [Nethercote, Aug 2023](https://nnethercote.github.io/2023/08/25/how-to-speed-up-the-rust-compiler-in-august-2023.html)
- **Parallel GCC** (Belinassi et al.).
  - 2020 GSoC: the intraprocedural optimizations take 75% of compile time, and parallelizing them gave 1.8x on 4 cores for large files. [GCC mailing list](https://inbox.sourceware.org/gcc/20200317202427.2cho2t3qjatw6suv@smtp.gmail.com/t)
  - The `-fparallel-jobs=` patches of August 2020 ranged from slight slowdown to about 1.9x on a quad-core i7. [Phoronix](https://www.phoronix.com/news/GCC-fparallel-jobs-Patches); [gcc-patches](https://gcc.gnu.org/pipermail/gcc-patches/2020-August/552569.html)
  - The 2022 paper "Compiling Files in Parallel: A Study with GCC": 35% faster GCC self-compilation against make -j alone, and up to 3.5x on single files. The numbers are from the abstract and the venue was not verified. [paper listing](https://www.bohrium.com/paper-details/compiling-files-in-parallel-a-study-with-gcc/1131538642460737537-2000000)
- **macOS process startup.**
  - An Apple Developer Forums thread (2026, macOS 26.6.2) measured `posix_spawn` returning in 0.179 ms, but 634 ms from spawn to `main()` in a pathological case. Apple DTS: macOS "has never been a speed demon" at starting processes. `DYLD_PRINT_STATISTICS` is undocumented in current dyld. [Apple Developer Forums](https://developer.apple.com/forums/thread/851167)
  - The posix-spawn gem documents, qualitatively, that `posix_spawn` is faster than fork+exec on OS X and does not depend on the size of the parent process. [posix-spawn](https://github.com/stereobooster/posix-spawn)
- mold: an insert-only concurrent map gave about 30% on linking with debug info, which shows that parallel scalability is limited by shared data structures. [mold commit](https://github.com/rui314/mold/commit/41b2fa73757b2e4eb44fc68e216a80d2a56af297)

### Inferences
- For vroom, with about 3k kernel TUs on 10 cores, intra-TU parallelism is mainly useful for the long tail: the last large TUs and the link. A work-stealing scheduler in vccd that splits large TUs per function at codegen is a better investment than a parallel front end.
- The large spread in macOS spawn-to-main times (sub-ms up to hundreds of ms) motivates measuring the shims' (`vcc`/`vas`/`vld`) spawn cost locally. Keep them small and statically linked, with few dylibs, and let vccd do all the work.

### Gaps
- No primary numbers were found for fork/exec, dyld and page-fault cost of a fresh process on Apple Silicon, or for Zig's parallel or incremental compilation (devlog numbers). A local microbenchmark is recommended.

## Profiling methodology for compilers

### Takeaway
The primary sources consistently measure instructions, cycles, cache misses, peak memory and wall clock separately (Zig with perf stat; rustc-perf with icount, wall and max-rss; simdjson with cycles/byte models). Instruction count is the stable metric to iterate on, and wall clock is the one to accept a change on.

### Cited Findings
- Zig PR #7920 reports cache misses, instructions, cycles, wall clock and peak RSS, and shows these can move in opposite directions: misses +34%, time -11%. [GitHub PR #7920](https://github.com/ziglang/zig/pull/7920)
- rustc-perf reports icount, wall time, peak memory and binary size, aggregated as an average over hundreds of benchmark results. [Nethercote, Aug 2023](https://nnethercote.github.io/2023/08/25/how-to-speed-up-the-rust-compiler-in-august-2023.html)
- simdjson models time as linear in the number of bytes and the number of structurals, which lets you predict cost from input statistics. [arXiv 1902.08318](https://arxiv.org/pdf/1902.08318)
- Lemire uses instructions per cycle to tell compute-bound from latency-bound: over 5 IPC for SIMD against a load-latency-bound table approach on an M2. [Lemire mirror](https://eklausmeier.neocities.org/lemire/blog/2023/09-04-locating-identifiers-quickly-arm-neon-edition)

### Inferences
- On macOS without perf, the equivalent is Instruments' CPU Counters, or kperf/kpc via the private `kperf.framework`. They give cycles, instructions and branch-mispredict counts per phase in vccd.

### Gaps
- `-ftime-trace`, Instruments CPU Counters and branch-mispredict analysis of lexers were not researched in depth for lack of budget.
