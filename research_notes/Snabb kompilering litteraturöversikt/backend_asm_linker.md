# Fast code generation, assemblers and linkers (tool speed, not output quality)

Scope: prior art relevant to vcc's emitter, built-in assembler (ELF writer) and linker for a cold arm64 Linux kernel build in under 2 s on an M1 Pro.  Research date November 2026; years noted per source.

## Baseline / fast code generators: what techniques and numbers exist?

### Takeaway
The fastest published code generators all collapse instruction selection, register allocation and encoding into one pass over the input with no intermediate IR, using a "virtual value stack" or precompiled templates.  Published speedups are 8-24x over LLVM -O0 (TPDE, 2025), about 100x over LLVM -O0 (copy-and-patch, 2021), about 180x over GCC -O2 (MIR, 2020), and about 9x over GCC -O0 for a whole compile+assemble+link (tcc).  "Optimizing-but-fast" designs like Cranelift gain only 10-40% over LLVM debug builds.

### Cited Findings
- **TPDE (Schwarz, Kamm, Engelke, TU Munich, arXiv May 2025)**: an LLVM-IR backend that compiles 8-24x faster than LLVM -O0 on SPECint 2017, with run-time performance "on-par" with LLVM -O0.  Targets x86-64 and AArch64.  Structure: an IR-specific adapter, then one analysis pass, then a single pass that combines instruction selection, register allocation and instruction encoding.  Instruction templates are written in a high-level language and lowered through LLVM Machine IR at build time, for portability. — [arXiv 2505.22610](https://arxiv.org/abs/2505.22610)
- **Copy-and-patch (Xu and Kjolstad, OOPSLA 2021)**: code is built by stitching together precompiled binary "stencils" whose holes are filled at code-generation time.  Reported numbers: about 2 orders of magnitude faster compilation than LLVM -O0 and about 3 orders faster than optimizing LLVM levels (TPC-H SQL queries), with code 14% faster than LLVM -O0; for WebAssembly, 4.9-6.5x faster compilation than V8 Liftoff with 39-63% faster code (Coremark, PolyBenchC); in a C-like language compiler, "code generation from an AST takes less time than building the AST". — [arXiv 2011.13127](https://arxiv.org/abs/2011.13127)
- **CPython JIT (PEP 744, 2024)** uses copy-and-patch: at build time LLVM/clang compiles each micro-op into machine-code templates (it needs clang's `musttail`), which are built into CPython; "The JIT has no runtime dependency on LLVM".  Building the templates adds 3-60 s to the CPython build; memory use is 10-20% higher than the interpreter (highest on aarch64-apple-darwin because of its page size).  The PEP gives no JIT compile-time numbers. — [PEP 744](https://peps.python.org/pep-0744/)
- **V8 Liftoff (2018)**: makes one pass over the opcodes of a function, with no IR, generating code while decoding and validating.  A compile-time-only "virtual value stack" records, for each value, whether it is in a register or a spilled stack slot.  Constants are recorded and only materialized when used; results get a free register; at block boundaries the stack state is snapshotted and merges emit reconciling moves.  The blog's speed comparison with TurboFan is only in charts, with no MB/s figures. — [V8 blog: Liftoff](https://v8.dev/blog/liftoff)
- **MIR (Vladimir Makarov, Red Hat, 2020, updated 2023)**: generating code for a sieve benchmark takes about 75 µs vs about 13.35 ms for GCC -O2 (about 178x).  Startup is about 1.3 µs vs 12.1 ms for GCC.  The C-to-MIR translator is about 15x faster than GCC -O2.  Design: only a few high-value passes (GCSE, SCCP, DCE, RA, instruction combining); a non-SSA IR, to avoid the cost of building and tearing down SSA; a single-pass liveness-based register allocator; no PIC; a compact binary IR format. — [Red Hat Developer: MIR](https://developers.redhat.com/blog/2020/01/20/mir-a-lightweight-jit-compiler-project)
- **tcc (Bellard)**: compiling the Links browser (about 1.95 M lines) took 2.27 s with TinyCC 0.9.22 (859 k lines/s, 29.6 MB/s) vs 20.0 s for GCC 3.2 -O0 (98 k lines/s), on a 2.4 GHz Pentium 4.  The timings include compile, assemble and link.  tcc has its own GNU-like assembler and generates x86 code directly. — [bellard.org/tcc](https://bellard.org/tcc/)
- **Cranelift / regalloc2 (Chris Fallin, 2022)**: register allocation dominated Cranelift's compile time; regalloc2 improved overall compiler speed by about 20% and reduced compile-time outliers.  Its speed came from: building liveranges in linear time, in reverse program order with appends only (IonMonkey's version is O(n²)); contiguous inline structs instead of linked lists; "half-moves" collected in one scan and then sorted to generate moves in bulk; 32-bit operands; bfloat16 spill weights; a sparse 64-bit-chunk liveness bitset.  The biggest win was having fewer bundles and liveranges.  In some runs about 10% of time went to `sort()`. — [cfallin.org regalloc2](https://cfallin.org/blog/2022/06/09/cranelift-regalloc2/)
- **Cranelift vs LLVM in rustc (debug builds)**: Rust's 2026 project goal reports Cranelift 12% faster than LLVM with `-Cdebug-info=line-tables-only`, an average gain of 10-15% per crate (negligible to about 30%), and 5% on an incremental build of Zed; the project targets 2x inside the cg_clif backend.  LWN (2024): building Cranelift itself took 125 vs 211 CPU-s (40% less CPU) and 29.6 vs 37.5 s wall-clock (20% less). — [Rust project goal 2026](https://rust-lang.github.io/rust-project-goals/2026/improve-cg_clif-performance.html); [LWN 964735](https://lwn.net/Articles/964735)
- **Zig self-hosted x86_64 backend (devlog, June 2025)**: became the default for Debug builds on Linux and macOS.  `hello.zig` builds in 275 ms vs 918 ms with LLVM, and the Zig compiler itself in 20 s vs 75 s.  Parallelizing code generation took the compiler build from 13.8 s to 10.0 s, at about 26% more peak memory.  Machine code is generated on multiple threads and gathered on a single linker thread, and "linking was sometimes the bottleneck".  An aarch64 backend was upstreamed in July 2025 (79% of behavior tests passing). — [Zig devlog 2025](https://ziglang.org/devlog/2025/)

### Inferences
- For vcc, a TPDE- or Liftoff-style design (sema'd AST, then one pass that does selection, allocation on a value stack, and direct encoding into a byte buffer) is the proven fastest structure.  Copy-and-patch fits vcc's "take data from clang once" rule: stencils could be generated by clang offline and shipped as generated tables, like `caps.c`.
- regalloc2's lessons (linear-time construction, flat arrays, bulk sort instead of incremental insertion) carry over to any allocator vcc might add; a full backtracking allocator is the slow end of the range.
- Gains from "fast optimizing" backends (Cranelift) are modest.  A kernel built at -O0-like quality in under 2 s points to the single-pass class (TPDE, tcc, Liftoff).

### Gaps
- I found no primary numbers for LLVM FastISel vs GlobalISel vs SelectionDAG at -O0, Go's SSA backend throughput, QBE, LuaJIT, SpiderMonkey baseline or Wasmtime Winch.  I did not search for them, because of the tool-call budget.
- The Liftoff vs TurboFan figures exist only in the blog's charts.
- I did not verify the TPDE paper's per-phase breakdown or how it writes ELF (the abstract does not say).

## Assemblers: textual asm vs an integrated assembler, encoding, relocations, ELF writing

### Takeaway
Primary, modern measurements of "emit text and re-parse it" vs an integrated assembler are scarce.  Old LLVM data suggests the assembler step was a large share of compile time, and every fast system (tcc, TPDE, Liftoff, Zig, MIR) encodes machine code directly with no textual step.

### Cited Findings
- 2007 LLVM mailing list (bzip2 benchmark): the gap between GCC producing an object file and producing assembly was about 14%, and clang plus the external assembler spent about half its time in the assembler, vs under 10% for GCC.  This is an old toolchain, so treat it as indicative only. — [cfe-dev 2007](https://lists.llvm.org/pipermail/cfe-dev/2007-December/000734.html)
- 2010: clang's integrated assembler built all of FreeBSD, but on the same list a maintainer said they had not yet measured its speed against the system assembler. — [cfe-dev 2010](https://lists.llvm.org/pipermail/cfe-dev/2010-October/011826.html)
- tcc counts assembly and linking inside its 859 k lines/s figure, using its own assembler. — [bellard.org/tcc](https://bellard.org/tcc/)
- TPDE does instruction encoding inside the same single pass as instruction selection and register allocation. — [arXiv 2505.22610](https://arxiv.org/abs/2505.22610)

### Inferences
- vcc already has a built-in assembler.  The main lever is to make sure the emitter calls the encoder directly (structs or opcodes to bytes), and that textual asm is parsed only for inline `asm()` and `.S` files.
- The kernel's arm64 `.S` files and inline asm still need a real text parser.  Its speed matters mainly for the `.S` TUs.

### Gaps
- I found no modern benchmark of LLVM MC (integrated) vs GNU as, or of textual round-trip cost.  MaskRay's blog (maskray.me) likely covers relocation, CREL and ELF writer cost, but the page I tried returned 404.
- I found no primary source on table-driven arm64 encoder speed.

## Linkers: mold, lld, wild, gold, ld-prime, and the Linux kernel link

### Takeaway
mold (paper accepted to ASPLOS 2027) is the state of the art.  It parallelizes every pass, and its own ablation shows "no single optimization dominates".  The output copy, symbol resolution and section GC dominate when serialized, and OS-level tricks (huge pages, fallocate, mimalloc, forked child, fast teardown) are each worth 10-60%.  The kernel's vmlinux link is 3-4 full sequential links (kallsyms fixpoint plus BTF), so per-link latency is multiplied.

### Cited Findings
- **mold paper (arXiv 2608.23228, ASPLOS 2027)**: 2.4-16.1x faster than lld, and up to 112x faster than GNU ld.  "no single optimization dominates; the speedup comes from the cumulative effect of parallelizing all passes." — [arXiv abs](https://arxiv.org/abs/2608.23228)
- mold techniques, per pass — [arXiv html](https://arxiv.org/html/2608.23228v1):
  - Parsing: all inputs, archive members included, are parsed in parallel.  Symbol names are interned so identity is pointer equality; names are binned by hash into thread-local buffers and deduplicated per bin, with no shared concurrent structure.
  - Symbol resolution: each file claims the symbols it defines with an atomic CAS on an owner field, and a precedence table picks the winner.  Archive members are included by a parallel liveness walk, so `--start-group` has no effect.  COMDAT groups are claimed the same way.
  - String merging: a custom concurrent hash map, pre-sized from a HyperLogLog estimate of the distinct-string count, so it never resizes.
  - Relocation scanning: GOT/PLT needs are set as per-symbol atomic flags using relaxed atomic OR, with no locks.
  - Section GC: parallel mark.  ICF: parallel hash-based color refinement.
  - Layout: two-level parallel prefix sum over groups of about 10 k input sections.
  - Range-extension thunks: one linear scan with two cursors.  On ARM64 Firefox debug they cost 0.07 s in mold vs 1.01 s in lld.
  - Output: mmap'd, and each input section is copied and relocated into its own disjoint region in parallel.  The build ID is BLAKE3 in a Merkle tree over 4 MiB blocks (23 ms for 2.5 GiB).
- mold system-level tricks (Firefox debug link) — [arXiv html](https://arxiv.org/html/2608.23228v1):
  - Huge pages on the output (`MADV_HUGEPAGE`): 1.46 to 0.89 s.
  - One `fallocate` before mapping: copy pass 0.82 to 0.24 s.
  - mimalloc at 0.89 s vs glibc 1.15 s and jemalloc 1.03 s.
  - Overwriting the existing output in place saves about 0.3 s on a 4.5 GiB Chromium binary.
  - Parallel `MADV_DONTNEED` teardown saves 0.3 s when retrofitted into lld.
  - A forked child does the work and the parent exits once output is written: about 10% of user-perceived time.
- mold numbers — [arXiv html](https://arxiv.org/html/2608.23228v1):
  - Ablation, overhead when one pass is serialized, on a 0.91 s baseline: output copy +542%, symbol resolution +125%, section GC +114%, build ID +62%, input parsing +52%, relocation scanning +43%, string merging +40%.
  - Per pass, lld vs mold: parse plus resolve 1.29 vs 0.17 s; GC 0.85 vs 0.06 s; string merging 0.31 vs 0.03 s; relocation scan 0.10 s for both; output copy 0.50 vs 0.23 s.
  - Scaling: 1.9x at 2 threads, 6.3x at 8, 13.5x at 32.  Single-threaded mold (12.2 s) is slower than lld (11.4 s).
  - On an M1 Ultra (16 cores), Firefox debug links in 1.02 s with mold vs 3.26 s with lld.
- mold README (2026 numbers), M1 Ultra P-cores on Asahi, lld / wild / mold: Clang 21 release 0.30 / 0.15 / 0.14 s; Clang debug 4.40 / 2.78 / 2.96 s; Chromium debug 9.54 / 3.49 / 2.22 s.  On a Threadripper 7980X, Chromium debug takes 16.64 / 3.98 / 1.65 s. — [rui314/mold](https://github.com/rui314/mold)
- **lld design principles**: "do less rather than do it efficiently"; read section contents and relocations lazily; do each expensive hash lookup once and keep the handle; replace symbols in place so earlier pointers stay valid; remember archive symbols instead of rescanning archives; use no abstraction layer over native input sections; sections copy themselves into mmap'd output.  At Chrome scale (17 k files, 6.3 M symbols, 13 M relocations) one extra hash lookup per symbol costs about 10%. — [lld NewLLD](https://lld.llvm.org/NewLLD.html)
- **wild (Rust)**: aims to be "very fast for iterative development".  Incremental linking is planned but not implemented.  It supports x86-64, AArch64, RISC-V, LoongArch64 and PPC64LE on Linux, plus wasm32; Mach-O is planned. — [wild-linker/wild](https://github.com/davidlattimore/wild)
- **Apple ld-prime (Xcode 15, 2023)**: a third-party recap of Apple's State of the Union says it was redesigned from the ground up and is "up to 5x faster", with debug binaries up to 30% smaller.  I did not verify this against Apple's own transcript.  Chromium's docs say it is faster than lld for local macOS development. — [speakerdeck recap](https://speakerdeck.com/kateinoigakukun/the-new-linker-in-xcode-15); [Chromium apple_platform_linkers.md](https://chromium.googlesource.com/chromium/src.git/+/main/docs/apple_platform_linkers.md)
- **Linux kernel vmlinux link** (`scripts/link-vmlinux.sh`, Linux 7.4-rc1 in `work/linux`), in order — [local source: work/linux/scripts/link-vmlinux.sh](https://github.com/torvalds/linux/blob/master/scripts/link-vmlinux.sh):
  - `.tmp_vmlinux1` is linked if KALLSYMS or DEBUG_INFO_BTF is enabled, then `gen-btf.sh` produces `.tmp_vmlinux1.btf.o` (pahole) and BTF_ids.
  - kallsyms "four step process": a dummy `__kallsyms`, then link 1, then kallsyms gives a `.o` of the right size, then link `.tmp_vmlinux2` (addresses shift), then a correct kallsyms `.o`.
  - A third link `.tmp_vmlinux3` runs if the kallsyms object's size or symbols changed, since a larger image "may have expanded ... more linker branch stubs / trampolines".
  - The final `vmlinux` link follows, then `resolve_btfids --patch_btfids`, then `sorttable`, then a System.map check.
  - The temporary kallsyms links use `strip_debug=1`.

### Inferences
- With the default config, a kernel build runs 3 or 4 full vmlinux links plus nm, kallsyms, pahole and sorttable.  A "compiler as linker" in vccd that keeps the linked image in memory could compute the kallsyms fixpoint itself: re-layout only, without re-reading inputs.  That is a large win specific to this project, and it needs no change to make.
- mold's per-pass profile maps directly onto 8 P-cores: the output copy/relocate (mmap, fallocate, huge pages are less available on macOS) and symbol resolution are what must be parallel first.  Single-threaded, mold is no faster than lld, so its advantage is entirely parallelism.
- At about 1 s for a 2.5 GiB debug link on 16 cores, mold suggests that a kernel image (tens of MB without debug info) can be linked in tens of ms per pass if parallelized.  This is extrapolated, not measured.
- The forked-child trick, the skipped teardown and in-place output overwrite are cheap to copy into `vld`.

### Gaps
- I found no gold design or speed source, no ld.lld `--threads` scaling numbers apart from mold's paper (lld reaches 2.6x at 16 threads), and no measured cost of ICF in lld.
- I found no measured timings for the kernel's link steps (kallsyms, pahole, modpost).

## Skipping the object file: in-memory linking, "compiler as linker", incremental patching

### Takeaway
tcc (compile, assemble and link in one process) and Zig (codegen threads feeding an in-process linker thread, with incremental compilation via `-fincremental`) are the working precedents.  Zig reports linking as its bottleneck once codegen is parallel.

### Cited Findings
- tcc can run C source directly with "No linking or assembly necessary", and its throughput figure covers the whole pipeline. — [bellard.org/tcc](https://bellard.org/tcc/)
- Zig: machine code is produced on multiple threads and gathered on a single linker thread, linking is sometimes the bottleneck, and linker improvements are planned (June 2025).  Incremental compilation (`-fincremental`, often with `--watch`) was near-stable in 2025 and gave a "near-instant" development loop by October 2025. — [Zig devlog 2025](https://ziglang.org/devlog/2025/)
- wild targets incremental linking as a future goal. — [wild](https://github.com/davidlattimore/wild)

### Inferences
- vccd is already persistent, so it can keep per-TU sections and symbols in memory and link without writing or re-reading `.o` files.  It still has to emit `.o` files for make's dependency tracking and for modpost/objtool, unless those are shimmed too.
- Zig's lesson is that a single linker thread becomes the bottleneck after codegen is parallelized.  vcc's linker should be parallel per pass (as in mold) from the start.

### Gaps
- I did not fetch a primary source for Zig's in-place binary patching (the 2025 devlog page does not mention it), Go's linker design, or Microsoft incremental linking.
