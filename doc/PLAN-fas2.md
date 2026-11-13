# Phase 2: own toolchain, cold kernel build < 2 s

**Goal:** `make ARCH=arm64 CC=vcc AS=vcc LD=vld …` builds `Image` from an empty O= in under 2 s
wall time (M1 Pro, 10 cores), with the same config as in phase 1. The kernel boots Debian 13 to
`is-system-running = running`.

**Rules:**
- No part depends on clang/LLVM or binutils.
- make/Kbuild stays. Stoakes' kbuild-speedup v5 is applied.
- Every part must have state-of-the-art performance:
  - SIMD
  - parallelism
  - minimal allocation
  - mmap instead of read

## Starting point (measured 2026-11-07)

| | |
|---|---|
| Phase 1 final state | 23.5 s (clang -O0 via vroomd + PCH), ~29–32 s when the machine is used for something else |
| Stoakes v5 | no measurable difference: no modules, no objtool on arm64, no rust |
| TUs | ~840 C + ~70 `.S` |
| Headers per TU | ~880 files, 4–9 MB of source text |
| Total read if every TU is preprocessed | 3.4–8 GB |
| Unique files / bytes | 3,114 / 44 MB |
| Before the first CC | ~6–7 s (Kconfig `cc-option` tests, syncconfig, asm-offsets) |

The budget for 2 s with 10 cores is ~20 core-seconds in total, so **≤ 15 ms of core time
per TU** including headers. That requires the header work to be shared: the 44 MB of unique text
may be processed about once, not 840 times.

## Architecture

```
make ──► vcc (shim, ~100 µs) ──unix-socket+SCM_RIGHTS──► vccd (server, one process, thread pool)
             │                                              │
             └── as/ld/objcopy/nm shims likewise            ├── header cache: tokens + macros + declarations
                                                            ├── TU jobs: lex → pp → parse → sema → emit (arm64)
                                                            ├── built-in assembler (.S and inline asm)
                                                            └── built-in linker: objects in memory → vmlinux → Image
```

### 1. Shims (`vcc`, `vas`, `vld`, `vobjcopy`, `vnm`, `var`)
- Static binaries of ~100 lines of C, without libc init overhead (`-nostartfiles` where possible).
  They send argv, cwd, env delta and fd 0–2 via SCM_RIGHTS and wait for the status.
- `cc-option`/`as-option`/`ld-option` tests are answered by the server from a table of
  what it supports. No compilation is needed, so the ~6 s before the first CC disappear.
- `-E`, `-S`, `-M*`, `-Wp,-MMD` are supported, because Kbuild uses them.

### 2. Lexer (SIMD)
- Source files are mmap'ed once per build (read-only, `MAP_PRIVATE`) and the mapping is shared
  by all threads.
- **Classification with NEON:** 16–64 bytes per iteration are classified into ident, digit,
  whitespace, `"`, `'`, `/`, `\`, `#` and newline via table lookup (`vqtbl1q_u8`) and
  bitmasks. Token boundaries are found with `ctz` on masks. Same technique as simdjson.
- Continuation lines (`\`+newline) and trigraphs are handled on a slow path, only where the
  mask shows `\`.
- **Tokens:** 8–16 bytes each, with kind, flags, offset and length. They are written to an
  arena per file. Identifiers are interned directly with a hash (the CRC32 instruction) and
  a lock-free open-addressed table shared between threads.
- **Tokenize once:** every unique file is tokenized once per build and the result is
  immutable afterwards. 44 MB ≈ 10 M tokens, and at ~1 GB/s per core that takes
  ~5 ms on 10 cores.

### 3. Preprocessor
Background and measurements are in `RESEARCH-preprocessor.md` (2026-11-07). Clang spends
61 % of an -O2 compilation on `-E`. The build reads 5.6 GB of source text, where every file is
preprocessed 121 times.
- Works on token streams, not text.
- **Expansion according to clang's model, not Prosser hide sets:** a "disabled" bit per macro
  and a paint bit per token. This gives exact clang parity without allocation. The contexts
  point into immutable token arrays, and only function-like macros write to a
  thread arena. Fast path for object-like macros with 0 or 1 tokens. `#define` is compiled to an
  opcode template, and arguments are pre-expanded lazily, once per argument.
- **Per file, once and shared:** directive skeleton with `#if`/`#else`/`#endif` links, so
  that a false block becomes a jump. The include-guard macro is stored in the token cache.
- **Include lookup without syscalls:** directory cache (`getattrlistbulk`) plus
  (path id, name) → file. The I/O threads pre-read targets as soon as they are known.
- **Macro table per TU indexed by identifier id:** two-level array with 16 KB pages
  and copy-on-write against shared snapshots. A lookup costs two loads and no
  hashing.
- **Locations lazy and sparse:** one record per expansion, and line numbers are computed only on demand.
- Pull model towards the parser.
- `__has_include`, `__has_attribute`, `__has_builtin`, `_Pragma`, `__COUNTER__`,
  `push_macro`/`pop_macro`, `#include_next`.

### 4. Header cache (the most important part for 2 s)
- Almost all TUs begin with the same `-include` files and the same large headers
  (`linux/kernel.h`, `mm.h`, `sched.h`, …) in the same macro context. Apart from `KBUILD_*`,
  the macro context differs only in small details.
- **Prefix sharing, dynamic and without a PCH file:** TUs are grouped by identical
  flag signature (`-D`/`-I`/`-include` except `KBUILD_*`). The first TU in a group builds the
  state incrementally. Each top-level `#include` without a preceding
  code dependency forms a node in a **prefix tree**: the state after `#include <a>`,
  then `<b>`, … Following TUs descend the tree as far as their include sequence matches
  and continue from there.
- A state consists of the macro table, declarations (symbol table, types) and
  emitted code for inline functions. It is immutable and shared between threads.
- **Read set per region** (after Bothner 2003 and GCC's `cpp_valid_state`, but exact).
  Each top-level #include region records:
  - macros read, with their value, including "undefined"
  - the define/undef delta
  - the `__has_include` answers
  - the `__COUNTER__` delta
  - the pragma state

  Regions that use `__FILE__`/`__LINE__`/`__INCLUDE_LEVEL__` take them as
  input or are not cached. No known compiler shares such state between threads
  in one process, so this is a new design: publish once, never free, no refcounts.
- If macros such as `KBUILD_MODNAME` are used under the prefix, this is recorded as a
  dependency on the node. Only those nodes then become specific per TU, and correctness
  is preserved without the `-fno-validate-pch` trick.

### 5. Parser + sema (resolver)
- **Parser:** hand-written recursive descent for C11 + GNU C, to the extent the kernel
  uses it. That includes:
  - statement expressions, `typeof`/`__auto_type`, attributes,
  - `asm`/`asm goto`, designated initializers, nested functions (not used; rejected),
  - `__builtin_*`, `_Generic`, `__int128` and vector types to a small extent.
- **AST:** flat and index-based, with nodes in arenas and 32-bit indices instead
  of pointers. Types are interned, so two equal types are the same index.
- **Sema in the same pass as parsing:** constant expressions are evaluated immediately, because
  `BUILD_BUG_ON`, `__builtin_constant_p` and `sizeof` in `#if`-adjacent code are used all the time.
- **Inlining and constant folding for `__always_inline`** and for constant arguments,
  which the kernel requires (phase 1 showed `"i"` operands). This is not an "optimization" but
  semantics that the kernel presupposes. The -O0 patch from phase 1 thereby becomes unnecessary.
- Dead code behind constant conditions is not emitted. This replaces the `vroom-ld` stubs.
- Functions are compiled lazily: `static inline` in headers is emitted only if used in the TU.

### 6. Emitter (arm64)
- Directly from AST to machine code, without IR, in the style of tcc but with a better
  stack allocation. All local variables live in stack slots, with a simple
  register cache for expression trees.
- Relocations and sections (`.text.*`, `.init.text`, `__jump_table`,
  `.altinstructions`, `__ex_table`, `__bug_table`) are written directly to in-memory objects.
- **Debug info:** none. The kernel config has DEBUG_INFO off.

### 7. Assembler (`.S` and inline asm)
- GNU as syntax for arm64, as far as the kernel uses it:
  - `.macro`/`.irp`/`.rept`/`.if`, `.pushsection`/`.popsection`, `.altmacro` (rarely)
  - local numeric labels (`1b`/`1f`), expressions with symbols
  - all A64 instructions that occur, including system registers (`msr`/`mrs` with
    `sysreg-defs.h`)
- `.S` goes through the same preprocessor and then directly to the assembler in memory.
- Inline asm: operands are substituted (`%0`, `%[name]`, `%c`, `%l`, `%w`, `%x`), and then
  the text goes through the same assembler.

### 8. Linker + objcopy
- Kbuild runs `ar` for `built-in.a`/`vmlinux.a` and then `ld` with `vmlinux.lds`.
  Within the build the shims send only references to objects that are in the server's memory;
  ELF files are written to disk asynchronously and only when someone reads them (nm, modpost).
- **Linker script:** an interpreter for the subset that `vmlinux.lds` uses:
  - `SECTIONS`, `KEEP`, `SORT`, `PROVIDE`, `ASSERT`, `ALIGN`
  - address expressions, `AT()` and `/DISCARD/`
- **Parallel layout per output section.** Relocations are applied in parallel per
  input section, and `Image` is written directly (objcopy -O binary).
- modpost needs ELF on disk for `vmlinux.o`. It is written with a single `pwritev`
  from memory buffers.

### 9. Memory and parallelism, throughout
- **Thread model: CPU threads and I/O threads are separate.**
  - **CPU pool:** one thread per core, pinned via QoS class on macOS (P cores for the
    critical path: header cache and linking, E cores for independent TUs), with
    work stealing (Chase–Lev deque per thread). No CPU thread ever blocks on I/O.
  - **I/O threads, 2–4:**
    - Pre-read: mmap plus `madvise(MADV_WILLNEED)` and pre-touching of pages for
      headers that are needed soon, driven by the prefix tree.
    - Asynchronous writing of `.o`, `.cmd`/`.d` and `vmlinux`: `pwritev` from finished buffers.
    - On Linux later io_uring: one ring per I/O thread, with registered buffers.
  - **Shim socket:** one accept and dispatch thread with kqueue (epoll on Linux) that puts
    jobs in the CPU pool and replies when the job is done.
  - make `-jN` delivers jobs, and the server prioritizes by itself.
- **Arenas:** per thread and per TU, freed in one sweep. No `malloc`/`free` in hot loops.
  Large buffers are taken with `mmap` and `MAP_ANON`, and pages are reused between TUs
  (`madvise(MADV_FREE)`).
- **Shared, immutable data:** file mappings, token streams, interned strings and
  header-cache nodes. No locking after publication.
- **Profiling from day 1:** Instruments/`perf` equivalent with dtrace and
  `os_signpost`, plus internal counters (ns per phase and TU). Every step below is measured before
  the next is started.

## Steps

| # | Deliverable | Criterion |
|---|----------|-----------|
| 2.1 | `vcc` shim + `vccd` skeleton that forwards to clang, and answers `cc-option` tests from a table | Kconfig phase ~6 s → < 0.3 s, build still green |
| | **2.1a done** (`patches/0002-kconfig-parallel-shell.patch`): speculative parallel `$(shell)` in Kconfig, byte-identical config. conf 4.1 s → 1.0 s; first CC 7.2 s → 4.3 s. The rest is process cost (97 `cc-probe` at ~35 ms each, 7 `as-instr` at ~156 ms each via real clang `-cc1as`) and disappears when vcc/vas answer by themselves | |
| | **2.1b done** (`src/`): `vcc`/`vld`/`vas` shim (one binary, no stdio/malloc) + `vccd` (kqueue accept, thread pool, mmap buffers per thread). Probes are answered from the capability table `caps.c` in ~0.05 ms (the whole call 3.2 ms ≈ process floor 2.3 ms), the rest is forwarded to vroomd/vroom-ld. The table is minimal: no sanitizers, stack protector, PAC/BTI, SCS, auto-var-init; warning groups ⊆ clang (`gen-warnings.sh`) as long as clang is the backend. First CC 4.3 s → 3.4 s, cold build 22.2 s, boots to `running` | |
| | **2.1c profile before first CC** (dash as `SHELL`/`CONFIG_SHELL`, `patches/0003`): ~3.5 s on a loaded machine. The critical path is serial: make parse + SYNC ~0.5 s → fixdep ~0.3 s → conf objects + bison + ld64 ~0.4 s → **conf ~0.7–1.3 s** (≈240 shell commands, ~700 processes at 2–3 ms each; close to the process floor) → dtc/kallsyms/sorttable ~0.6 s (Kbuild makes `archprepare` wait for all of `scripts`) → bounds.s. Host tools with -O0 gave no gain (conf became slower). **Conclusion:** with make + Kconfig + host tools built by clang/ld64 the floor is around 1.5–2 s before the first CC, so < 2 s in total requires (a) that vcc can also build the host tools (Apple Silicon is AArch64: Mach-O output + libSystem ABI in the same emitter), (b) that `archprepare` waits only for `scripts_basic`, (c) fewer processes per probe (`cc-probe` without mkdir/rm) | |
| | **2.1d done** (b) and (c) from 2.1c, plus probes that were on the critical path. Kernel patches `patches/0004`–`0007` (branch `vroom-pf` in `work/linux-pf`, not yet in `work/linux`): (1) `archprepare` waits only for `scripts_basic`; the descent into the tree and `modules_prepare` wait for `scripts` (GCC plugins/recordmcount/Rust: as before); arm64's kapi headers (`cpucap-defs.h` etc.) are generated as a prerequisite of `archprepare` instead of in its recipe, in parallel with asm-generic (0007). (2) `filechk` skips `mkdir -p` and `rm -f` when they are not needed (4 → 2 processes per generated header); the config instance expands `CC_VERSION_TEXT`/`RUSTC_VERSION_TEXT`/`PAHOLE_VERSION` once instead of at every command (~20 × `vcc --version \| head` + pahole + rustc). (3) Kconfig creates a scratch directory (`$KCONFIG_TMPDIR`, removed at exit) and `cc-probe` does the `-Wno-` rewrite with parameter expansion: 7 → 2 processes per `cc-option`, no nested `$(shell)`. syncconfig: 241 → 143 commands, 9–11 → 4–6 rounds. server/caps.c: `as-instr` and `echo … \| $(CC) -x c -` are looked up in a table with clang's answers (unknown source is compiled for real with the same stdin); link probes (`cc-can-link.sh`) → no; `--version`, `-print-file-name=`, `vld --version` from caps.c; vld refuses `--pack-dyn-relocs=`/`-z pack-relative-relocs` directly (the relr script 187 → 32 ms). `.config` byte-identical and all 1,387 `.cmd` files identical against A. **A/B** (alternating, same private vroomd/vccd, `VCC_PP=0` because of the -E regression in main): first CC from make start 3.5/3.5/3.4 s → 2.3/2.3/2.3 s (0004–0006, load 4 → 50) and 3.5/3.5/3.9 → **2.2/2.2/2.3 s** (with 0007, load 19 → 58); cold build 22.4/23.5/23.9 → 21.2/22.5/23.4 s and 24.4/23.8/24.5 → 22.7/24.4/23.5 s respectively (the compile phase dominates and is noisy under load); boots to `running`, 0 errors. conf itself ~0.4 s quiet (yyparse incl. `$(shell)` 0.23 s, `menu_finalize` 0.17 s pure CPU in `expr_transform`). The shim is at the floor: `vcc --version` 3.4–4.1 ms = an empty `int main` (3.5–3.9 ms) against `/usr/bin/true` 2.6 ms (system binaries start faster); the vccd round trip ~0.1 ms. **Remaining before first CC (~2.3 s):** four parses of the top-level Makefile (outer+inner ~0.2 s, config instance ~0.2 s, the restart after `auto.conf`) with ~50 processes each, of which try-run probes at 4 processes each; fixdep 0.2 s and conf objects 0.3 s (vroom-cc + ld64); `menu_finalize`; `cc-version.sh` (`-E` of stdin) | |
| 2.2 | SIMD lexer + token cache | lexes all 3,114 kernel files; > 1 GB/s/core; tokens identical to clang `-dump-tokens` |
| 2.3 | Preprocessor | `vcc -E` gives the same token stream as `clang -E` for all 840 TUs (normalized comparison) |
| 2.4 | Header cache/prefix tree | `-E` for all TUs < 0.5 s wall time in total |
| 2.5 | Parser + sema | `-fsyntax-only` for all TUs without errors; constant expressions = clang |
| 2.6 | Emitter arm64 | compiles all TUs; per-file diff of symbols and sections against clang -O0; the kernel boots with vcc objects + lld |
| 2.7 | Assembler | all `.S` and all inline asm; byte-identical to clang `-cc1as` for `.S` |
| | **`.S` done**: 30/30 byte-identical, also via `vpp` (see Status 2.7); inline asm remaining | |
| 2.8 | Linker + objcopy | `vmlinux` and `Image` without lld; boots |
| | **2.8 done** (the vdso link in 2.12): `vlink` byte-identical to lld (30 ms against 340 ms), `vobjcopy`/`vnm` identical to llvm-objcopy/llvm-nm, Image boots | |
| 2.9 | Everything in memory, profiling, fine-tuning | cold build < 2 s, boots Debian to `running` |
| | **vccd compiles by itself** (see Status 2.9): all 877 `-c` of the kernel's `.c`/`.S` in vccd, 0 to clang; objects directly from the emitter without asm text; compile phase 16.7 → 11.7 s | |

### Status 2.2 (2026-11-07)
- `src/lex.c`, `intern.c`: structural lexer in the style of simdjson. 9 masks per 64-byte block
  (vld4q + tbl4), all token starts in a 64-bit window are computed at the same time, and
  token kind is chosen branch-free. Literals, comments, splices and UCN go via computed goto.
  Interning is done after the token loop: a thread-local cache with 2048 entries and a lock-free global table.
- Correctness (`src/lexdiff.py` against `clang -cc1 -dump-raw-tokens`, gnu11), identical on:
  - 3,225/3,225 build files (6.06 M tokens)
  - 5,489/5,489 random tree files + fuzz (11.2 M tokens)
- Performance on one P core (`lextest core`, 45 MB):
  - **0.99 GB/s** without interning (45 ms)
  - **0.64 GB/s** with interning (70 ms)
  - The goal of > 1 GB/s with interning is **not reached**. Interning costs ~10 ns per
    identifier (2.5 M of them). Prefetch and batched misses in the global table
    turned out slower.
- `lextest bench -j 8` (mmap + lexing through the token cache): 30 ms wall time for all
  3,225 files with a warm page cache.
- PMU measurement (`src/pmc.c`, `sudo src/lexpmc.sh`, same counters as Instruments
  CPU Counters). Interning adds per pass:
  - +91 M cycles
  - +160 M instructions (IPC 2.2)
  - +1.73 M L1D read misses
  - +655 k mispredictions

  The local cache itself costs ~8 ms. The rest is ~520 k lookups in the global table
  at ~40 ns each. They go out to SLC/DRAM, because 45 MB of source text per pass pushes out everything
  except L1.
- Tried and rejected (A/B, alternating runs):
  - larger local cache, both 1-level and 2-level (L2 size)
  - 2-way associative cache (mispredictions)
  - 48-byte NEON keys
  - keys inline in the global table's slots
  - two-phase batch with prefetch, with and without "pending" markers
  - software-pipelined prefetch D=8

  All turned out 3–15 % slower. Kept: global entries with the first 16 bytes
  inline, 2^20 slots, `always_inline`.
- `lextest idcheck`: every identifier's id spells the identifier (2.8 M, 0 errors).
- The token cache's real path (`lextest bench -j 8 [-r R]`: open + read + lex + cold
  interning) costs ~250 ms CPU per pass against 70 ms for pure lexing. More than half is
  kernel time:
  - `open()` costs 12–20 µs/file on 1 thread and ~43 µs on 8 threads, i.e. ~145 ms CPU
    per pass. For comparison `stat` takes 1.7 µs.
  - read, fstat and close are negligible.
  - Page faults cost 1.1 µs/page on 1 thread and 2.7 µs on 8.

  Changed: the source is read with `read()` into the arena instead of mmap. That gives 20 % fewer
  page faults and removes the double mapping for files without slack. Tried: `mlock` to
  pre-fault (worse) and `madvise` (no effect).
- The `open()` cost is real: same numbers from a plain terminal. It is
  independent of flags (O_EVTONLY, O_NONBLOCK, O_NOFOLLOW, openat). It does not depend on
  `com.apple.provenance` (SDK headers without xattr are equally expensive), not on FileVault /
  per-file keys (the sealed system volume is equally expensive), and not on the sandbox.
  Throughput levels off at ~3 threads, ~6 µs wall per open, i.e. serialized in the
  kernel. The floor for 3,225 files is ~20–30 ms wall.
- Cause, according to the XNU source (open1 → vn_open_auth, fp_close_and_unlock). Open + close
  do far more than stat:
  - MAC hooks: `mac_vnode_check_open`, `mac_vnode_notify_open` and
    `mac_file_notify_close`, across all policy kexts
  - APFS `VNOP_OPEN`, `VNOP_CLOSE` and `VNOP_INACTIVE` (closed source)
  - the global `vm_object_cached_lock` twice per cycle, because usecount goes
    0→1→0
  - `proc_fdlock` 3–4 times and atomic counters (`nfiles`, credential refcount)

  N processes level off at the same ~19 ms as N threads, so the bottleneck is system-global
  and not per process. Similar findings: Szorc, global locks in APFS (2018); Arko, APFS
  levels off at ~4 cores (2025). The only remedy is fewer open().
- Read-ahead: `vlex_prefetch()` queues files to 3 I/O threads (`VLEX_IO_THREADS`) that
  do open + read. `vlex_get()` only lexes, or itself takes over a file that has not yet been
  read. The lexer threads' CPU drops from ~250 to ~120–160 ms per pass. Wall time
  is ~35 ms, close to the I/O floor. Verified:
  - `idcheck` with and without prefetch
  - lexdiff 3,225 + 5,489 files
  - missing file

Every step is verified differentially against clang/lld, which are used only as a reference
and never in the build. A TU test (`bench/vcc-diff.py`) runs over all kernel files.

### Status 2.3 (2026-11-07): done
- `src/pp.c` (+ `pp.h`): pull model (`pp_next`), context stack without copying (files and object-like macro bodies point into the token cache's arrays), function-like expansions in a per-TU arena that is reset when the stack is down at file level. The expansion is a port of clang's TokenLexer logic: disable bit per macro (bit 0 in the macro table entry) + paint bit, pre-expansion only if the argument mentions a macro, pop of finished contexts before `(`, GNU `, ## __VA_ARGS__`, whitespace propagation (visible in `#x`), `__LINE__` = end of the expansion for ##-operands.
- Per file (shared, built once, published atomically in `vlex_file.pp`): directive skeleton with `#if`→`#elif`/`#else`/`#endif` links (a false group = one jump, no tokens touched) and include guard (`#ifndef X` / `#if !defined(X)`) → the file is skipped without being opened again.
- Macro table indexed by identifier id (two-level, 2048-entry pages), no hashing. Predefines + `__has_*` answers are taken from clang once (`gen-ppdata.sh` → `ppdata*.inc`), like caps.c; vcc never runs clang.
- **Acceptance:** `bench/vcc-diff.py cmp --kernel`: **840/840 kernel TUs token-identical to `clang -E -P`** (kind + spelling, lexed with vcc's lexer). References: `bench/vcc-diff.py ref` → `work/ppref/`.
- Speed (`vpp`, one TU per process): kernel/fork.c 78 ms cold / 29 ms with a warm token cache; clang -E 310 ms. async.c 49/15 ms against 170 ms.
- Host TUs (`scripts/`): Darwin predefines, clang's resource include + SDK `usr/include`, framework directories (`<A/b.h>` → `A.framework/Headers/b.h`), `__has_*` table with two columns (linux, darwin) generated over all SDK/kernel identifiers.
- `__VA_OPT__` (incl. placemarker at `##`), `#line` (affects `__LINE__`/`__FILE__`), assembler-with-cpp (`.S`, `.lds.S`, `.dts`: unknown directives and `# N "file"` pass through, `#` without a parameter, invalid paste leaves both tokens, `__ASSEMBLER__`), `-undef`.
- Directory cache (`dircache.c`): ENOENT open() costs ~3 µs and is serialized system-wide; 94 % of the misses are in directories that do not exist, so the cache only remembers whether a directory exists (one stat per directory, lock-free, shared between threads). Listing with getattrlistbulk was tried and was slower.
- `ppdriver.c`: clang argv, predefines, `-E -P` output and `-MMD`/`-MD` deps (clang's order, `./` stripped) behind `ppd_run()`; used by `vpp` and by `vccd`, which now runs `cc -E` jobs itself (`VCC_PP=0` → clang). Fatal errors longjmp, `pp_close` frees everything.
- **Acceptance:** `bench/vcc-diff.py cmp -j 6`: **911/912 TUs token-identical** (840 C + 39 .S + 32 lds/dts in the kernel, + host). The exception, `scripts/mod/modpost.c`, needs a generated `module-offsets.h` that clang does not find either. Via the `vcc` shim → `vccd`: vmlinux.lds, vdso.lds and empty_root.dtb identical to clang incl. dep files, 0.5–3.4 ms per job.
- Remaining → 2.4 (header cache/prefix snapshot) to get under 0.5 s in total.
### Status 2.4 (2026-11-07): header cache for `-E`, goal not reached (0.77 s against 0.5 s)
- Measurement rig: `vpp --batch JOBS -j N -C DIR [--cmp]` runs all TUs in one process with one token cache, as vccd does (`bench/vcc-diff.py batch [-j N] [--cmp]`). Starting point without cache: 912 TUs in **4.7 s wall / 28 s CPU** (`-j 6`).
- Shared macro objects: `#define` in headers is cached per directive location, the main file's and `<built-in>` definitions are made canonical on content, and builtins are process-global. The same macro then becomes the same pointer in all TUs, so macro values are compared with a pointer comparison.
- Region cache (only `pp_text`, i.e. `-E`): every `#include` is a region, including nested ones (up to 64 levels). The read set is exact:
  - macro value at entry, or only "defined" when only that was read;
  - include guards (checked first);
  - membership in `#pragma once`;
  - identifiers pasted together with `__COUNTER__`;
  - `__INCLUDE_LEVEL__`;
  - how many path directories the lookups depended on (prefix signature).

  An entry saves the define/undef delta, once additions and file records (for deps and positions), the `__COUNTER__` delta and positions (the values are rewritten on replay, also inside strings from `#`), and the text as a rep: own text plus references to the children's entries. A replay inside a region that is being recorded is added to the outer read set. Not cacheable: diagnostics, `#line`, push/pop_macro, `__DATE__`/`__TIME__`/`__TIMESTAMP__`/`__BASE_FILE__`. Up to 64 variants per key (file, path, directory, sys flag, flag signature). Everything is published lock-free (CAS) in shared memory and never freed.
- Small things from the profile: the `-E` buffer and the recording buffers are reused per thread (no munmap and no page faults per TU), and `__COUNTER__` digits are written without snprintf.
- **Speed (`-j 6`, shared machine with load 5–7): 0.77 s wall / 4.6 s CPU** (6× less CPU); `-j 10` gives 0.67 s; one thread 3.5 s (without cache ~22 s). 2.8 GB of 2.9 GB output is replayed from the cache (42k hits, 12.5k recorded misses).
- **Verification:**
  - `vcc-diff.py batch -j 6 --cmp`: 911/912.
  - The `-E` text and the dep files are byte-identical to `VPP_NOHC=1` for all 911 TUs.
  - `vcc-diff.py cmp -j 6`: 911/912.
  - `vcc-sema.py run`: 878/879.
- Why 0.5 s is not reached: two thirds of the CPU time goes to misses. 9.4k of them are due to include guards, i.e. that the same header is included where a child is already (or not yet) included. Each miss processes only the header's own text, because the children hit, but that is enough to dominate.
  - Tried: "guard-tolerant" hit, which skips a child that the TU has already included. It gave only 376 hits and was removed.
  - The dominant direction is the opposite: the entry was recorded when the child already existed, and this TU lacks it. It requires the reads to be ordered relative to the child's position (read position per input) or a prefix tree per include sequence.
  - Faster processing per token (the pp speed work) directly reduces the cost of the misses.
- Environment variables: `VPP_NOHC=1` turns off the cache, `VPP_HC_DEBUG=1` writes the cause per miss, `VPP_HC_MAX_MB` sets the memory cap (default 8 GB).
- **Token mode (`pp_next`, i.e. the parser):** same regions and read sets, but the key distinguishes text and tokens.
  - The recording takes the tokens that `pp_next` hands out. Spellings in per-TU memory are copied, locations are stored relative to the region's base, and `__COUNTER__` digits are saved as (token, byte position).
  - A hit pushes a `C_REPLAY` context that hands out tokens unexpanded (painted). The children's entries are spliced in, locations are moved to the TU's base and counter digits are rewritten (identifiers are interned anew). `pp_next` has a fast path for replay tokens.
  - **Verification:**
    - `--vcc-tokhash` hashes kind, SOL/SPACE, location, spelling, id and the file list per TU: identical to `VPP_NOHC=1` for all 911 TUs.
    - `vcc-sema.py run` 878/879, `layout` 878/879 (18.8 M checks).
    - `-E` is still byte-identical, and deps likewise.
  - **Speed (`-j 6`, load 8–11):**
    - the token stream for all TUs: 1.74 s / 10.4 s CPU against 3.1 s / 18.5 s without cache;
    - `-fsyntax-only` for 879 C TUs: 3.77 s / 22.6 s CPU against 4.80 s / 28.7 s.

    541 M tokens are replayed, at ~7–8 ns per token limited by memory bandwidth (24-byte `ptok`). The parser is now the larger part, so declaration replay (RESEARCH-preprocessor §6.4) is the next big win.
- Remaining:
  - declarations per region for the cc path. The first form is done (`src/ccsnap.c`, RESEARCH-preprocessor §6.7): the parser's state after the TU's initial top-level headers is saved in a trie per cache entry, and TUs that begin the same way restore it instead of parsing.
    - 834 of 879 TUs restore, and 98 M tokens (16 %) are skipped.
    - `-fsyntax-only` 2.96–3.25 s against 3.33–3.81 s, and `-S` 3.13 s against 3.51 s.
    - `-S` output and layout checks are identical with and without the cache (`VCC_NODC=1`) for all TUs.
    - The memory is 256 MB (`VCC_DC_MAX_MB`).
    - What remains is relocatable replay per region at all depths, which covers the remaining 84 %;
  - ~~read order per region against the guard misses~~: tried and shelved (RESEARCH-preprocessor §6.6). With splitting there were 6–8 % fewer misses, but 4× slower `-E` and 101 TUs with wrong output;
  - ~~memory: 2.8 GB stored at `-j 6`~~ (2026-11-08): the cap is still `VPP_HC_MAX_MB`, but an entry now stores much less:
    - Profile first (`VPP_HC_DEBUG=1` writes bytes per kind, `VPP_HC_PUB=1` one line per entry): in `-E` mode there was 1.33 GB of entry data, of which macro deltas 628 MB, reads 308 MB, own text 233 MB and file records 141 MB. The old "stored" figure counted other threads' allocations (`shared_used`); it is now counted exactly (`hsalloc`).
    - Reads and deltas repeat the children's runs in every enclosing header. They are now split into content-defined chunks (cut by id, ~32 elements, at most 256) that are stored once in a lock-free table; file names, own `-E` text and token arrays are also stored once (`hc_blob`).
    - Stored tokens are 12 bytes (`htok`) instead of 24 (`ptok`): identifiers as id, other spellings as a 32-bit offset into a shared blob, location relative to the entry, kind/flags/16-bit length. Spliced identifiers and long spellings go to a full `ptok` (HF_X).
    - **Measured (`-j 6`, load 10–13), main against now:** `vcc-diff.py batch --cmp --kernel` 1.33 GB → 0.33 GB stored, max RSS 2.07 → 1.04 GB, 3.2 s wall and ~19 s CPU both. `-fsyntax-only` batch (840 TUs) 3.6 GB (racy figure; ~1.1 GB tokens) → 0.44 GB stored, max RSS 3.21 → 1.36 GB, wall 1.39 → 1.48 s, CPU 8.3 → 8.9 s. The cost is in publishing (hashing and comparison of chunks); pure replay (`--warm 1`) went 2.98 → 2.54 s wall at the same number of cycles.
    - Entries as trees (2026-11-08): an entry stores only its own reads, its own last writes (`HT_O`) and a list of child entries. Each child has the ids that the parent wrote before the child; the child's reads of them are not inputs. The delta is applied child by child and then the entry's own. At lookup a child's outcome is memoized per `hc_try`, and the counter names (`__COUNTER__`) are checked last. A variant beyond 64 per key is published but not listed, so that the parent can have it as a child. At the memory cap the enclosing levels become non-cacheable. Stored `-E` 330 → 292 MB, tokens 430 → 361 MB, max RSS 735 → 691 MB and 1.35 → 1.27 GB respectively; instructions 77.3 → 76.3 G, cycles equal within the noise (load 6–9). Publishing's chunks almost vanished (`hc_vec` 193 → 14 samples), but now the validation of the children and the walk through the child's reads and writes when it is added to a parent cost about as much.
    - Verification: `--vcc-tokhash` identical to `VPP_NOHC=1` for all C TUs, `vcc-diff.py cmp -j 6` 911/912, batch `--cmp` 873/873, `vcc-sema.py run` and `layout` 878/879, `fuzz-regress` green. The TSan run was aborted (too slow on the shared machine) and is not done.
  - vccd does not re-read files and already trusts that they are immutable, and the cache inherits that.
### Status 2.5 (2026-11-07): parser + sema, first version done
- 2.4 (header cache) is deliberately postponed: first the whole chain, then speed.
- `src/cc.c` (+ `cc.h`): recursive descent for GNU C11 with sema in the same pass. Types are interned (qty = index<<3 | qualifiers), records/symbols/nodes are 32-bit indices into per-TU arrays reserved with mmap. Every expression gets its type, implicit conversions become N_CAST nodes, and constant expressions are evaluated on demand with clang's distinction between ICE and foldable (null pointer constants, i.e. `__is_constexpr`, depend on it).
- Record layout is ported from clang's ItaniumRecordLayoutBuilder (AArch64: bit-field type alignment, zero-width bit-fields, packed/aligned per field and record, `#pragma pack`). Enum types as in clang: all enumerators are `int` if all values fit, otherwise the enum type.
- GNU/clang extensions that the kernel uses: statement expressions (also with a label last), `typeof`/`__typeof_unqual__`/`__auto_type`, `_Generic`, designated initializers with ranges and brace elision, compound literals, `asm goto` with operands, `__label__`, `&&label`/`goto *`, case ranges, `?:` without the middle operand, `-fms-anonymous-structs`, `-fshort-wchar`, `counted_by`, ~120 builtins (foldable where clang folds). Host TUs: nullability qualifiers, block pointers in SDK prototypes, implicit int in system headers (pp now marks system headers).
- **Acceptance:** `bench/vcc-sema.py run`: **878/879 TUs without errors** (840 kernel + 38 host; modpost.c lacks a generated header for clang too). `bench/vcc-sema.py layout`: vcc writes `_Static_assert` for sizeof/_Alignof/offsetof for all records/enums/typedefs at file level, sizeof and type (`__builtin_types_compatible_p`) for all global objects and functions, and value and type for all enumerators; clang checks them against its own -E output: **18.8 M checks, 878/879 TUs green**. (clang's `-fdump-record-layouts-complete` could not be used: it dumps before `} __packed;` has been applied.)
- Speed: kernel/fork.c `-fsyntax-only` 84 ms incl. pp (pp alone ~75 ms), clang 464 ms.
- Remaining to 2.6: the types inside function bodies are not verified against clang (comes with the object comparison), static initializers are not checked for constness, VLA and declarations after a label are approximated, `__builtin_constant_p` after inlining (the emitter's job), no K&R.
### Status 2.6 (2026-11-08): emitter, all 840 kernel TUs match clang, the kernel boots Debian
- `src/emit.c` (+ `ccint.h`, cc.c's internal structures broken out): arm64 code directly from the AST, without IR. Integer folding while the code is generated (every expression becomes a constant, symbol address + offset or register), a reachability flag so that no code is generated behind constant conditions (`IS_ENABLED`, `compiletime_assert`, constant `switch`, after `return`/noreturn), `__always_inline` is expanded with constant arguments bound to the parameters (asm `"i"`, `alternative_has_cap_likely`, `arch_static_branch` see constants), lazy emission (static functions/objects only if emitted code references them), local variables in stack slots, temporaries in x9–x15/x19–x28. AAPCS64 (records ≤ 16 bytes in registers, x8 for larger, varargs with a 64-byte save area), inline asm with constraints r/i/n/I–N/Z/m/Q/o/p, modifiers w/x/c/a/l/H/n, `asm goto` with outputs, bit-fields, 128-bit integers, the kernel's data initializers (ranges, overwriting, flexible arrays), alias/weak.
- Instruction records (16 bytes) through a narrow builder interface; the printer makes assembly text that clang `-c` assembles today, an encoder (a64.c, another session) can take over without changing codegen. Functions are independent: worker threads take them from a queue with their own record, text and binding buffers (mmap, 32-bit indices); the pieces are stitched together in a fixed order, so output is byte-identical regardless of thread count.
- What the kernel requires beyond -O0: `BUILD_BUG_ON` in ordinary `static inline`/`static` functions that fold only when the function is inlined where the arguments are constant (`io_kiocb_cmd_sz_check`, `patch_map` → `fix_to_virt`). Solution: inline functions that call an `error()` function are inlined; a static function where an `error()` call survived is marked and the TU is regenerated with it inlined (rare). Remaining `error()` calls are ones that clang removes with value ranges (`min()` signedness in `while (len > 0)` in `copy_string_kernel`) and are removed, like the kernel's own -O0 variant (`((void)(condition))`); a trap there hit at boot. `__builtin_constant_p` follows clang's rules: folded in the front end → 1; side effects or pointers → 0; otherwise the operand after inlining.
- **Acceptance:** `bench/vcc-obj.py` (new) builds clang references with each TU's flags and compares per object: defined global symbols (binding, type, section, size for objects), that no must-be-dead functions are referenced (`__compiletime_assert_*` etc.), and the relocations inside global data objects. Differences that follow from clang -O2 inlining more (other undefined symbols, code and asm table sections such as `.altinstructions`/`__bug_table`, `.text.unlikely.`) are notes. **840/840 kernel TUs green**. Execution tests in qemu (`src/test/emit/run.sh`, bare-metal virt, UART output against clang -O2): t1–t6 green.
- **Dead code that -O2 removes (2026-11-08):** to link and boot, more of what clang -O2 does is needed: small `static inline` functions (≤ 40 AST nodes) are inlined (`scs_is_enabled()`); local variables that are only assigned integer constants are followed along forward edges (the value follows the jump to the label, joins keep what all paths agree on, loop headers and unknown targets forget; e.g. `alternative_has_cap_unlikely` and `enable_scs` in `map_kernel`); `__builtin_strcmp` of two string literals is folded (`vruntime_cmp`); discarded expressions without side effects are not generated (`(void)(RCU)`); unused scalar arguments to inlined functions are not generated. User labels are forced reachable only if they are targets of backward jumps or `&&`. An internal error catches backward jumps to a label that was dropped or placed with known values. `bench/vcc-obj.py` lists symbols that vcc leaves undefined that are not in vmlinux.
- **Boot (2026-11-08):** `bench/vcc-kbuild.py DST` (new) copies build-o0, replaces the objects of all 838 kernel TUs with vcc objects (`vpp -S`, clang assembles), links with `kmake ... LD=ccs/vroom-ld Image`. The vDSO is built by clang (requires PIC, missing). vroom-ld stubs 28 symbols that only dead code references (clang -O0: 94, vcc's are a subset). **Debian 13 boots to `running` without errors** (`bench/boot-test.py`, `results/boot-20261108-001340.log`). Bugs found along the way (bisection per object against clang objects, ~17 s per step): the `for` initializer's cleanup depth in the prepass (`scoped_guard` unlocked twice → preempt counter wrong), an unused `va_list` (32-byte record) copied from address 0.
- **Code quality, first step (2026-11-08):** measured against clang's -O2 pipeline (kernel flags) on 80 random kernel TUs, statically (instructions in the objects): vcc 2.86× clang -O2, clang -O0 2.78×; with clang's -O2 backend alone 1.46×, + mem2reg/SROA 1.15×, + InstCombine/SimplifyCFG/EarlyCSE 1.10×, + inliner 0.88×, + GVN/LICM/DSE 0.86× (full -O2 1.00×: unrolling and more inlining cost size). Classification of vcc's sp/fp loads and stores: ordinary local variables 31 % of the instructions, reload directly after store 6.6 %, slot→slot copies 6.2 % (inlined calls bound parameters and results to slots), `b` to the next label 6.5 %, spills around calls 0.2 %. Two measures: (1) `b` to a label directly after is removed in `finish_function`; (2) an inlined function's parameters that the body never writes (taking the address counts as a write) keep the argument's register (`B_REG`, read = `mov`, narrow types are extended once as a load would), and the result comes in a register (`fnctx.retreg`), as long as at least 8 temporaries are free. Result on the same 80 TUs: instructions 370,852 → 339,546 (−8.4 %, 2.62× -O2), sp/fp operations 174,156 → 135,005 (−22 %), `b` to next 24,060 → 6, reloads 24,313 → 11,102, copies 22,845 → 8,533; spills around calls 608 → 5,360 (parameters in x9–x15 across calls in the body: the next step is callee-saved registers for them). Compile time unchanged (fair.c 0.10 s). Verified: run tests t1–t6, `bench/vcc-obj.py` 840/840 (same 31 undefined symbols as before), `bench/vcc-fpcheck.py` 840/840, all 840 kernel TUs from vcc boot Debian 13 to `running` (`results/boot-20261108-231928.log`). **Correction:** the boot log above (`boot-20261108-231928.log`) was a clang kernel: kbuild rebuilt the freshly copied tree with clang (854 CC lines), see the next item; the changes are boot-verified there.
- **Local variables in registers (2026-11-08):** the prepass counts each local variable's uses weighted by loop depth (8× per level, `luse`) and marks those that need memory (`lnoreg`: address taking, asm operands as output or m/g/X, va_start, `{}` initializers; distinct from plain writes). The emitted function's (not inlined bodies') most used scalar local variables and parameters get callee-saved registers from x28 downwards for the whole function (`B_LREG`, lvalue `L_VREG`: read `mov`, write `mov`/extension as a load from a slot would give; address or memory access is an internal error). The registers are taken from the temporary pool. If the temporaries run out the function is regenerated without register variables and with more free temporaries in inlined calls (`INLINE_FREE_FAST` 5 → `INLINE_FREE_SAFE` 8). New `codegen/opt.c`: liveness analysis over the function's records (blocks at labels and after jumps; inline asm, `I_WORD`, brk read all registers; functions with `br` are left alone), forward copy propagation (`mov t, v` → reads of t read v), backward (`op t; mov v, t` → `op v`), dead `mov` removed. Measured (80 TUs, statically): `VCC_LREGS`=0/4/6/8/10 gives 311,794/292,299/291,677/291,417/… instructions; without opt.c the count increased (every read a copy, `mov` 39 k → 76 k). With 6: instructions 339,546 → 291,677 (−14 %; since the session's start 370,852, −21 %; 2.25× clang -O2, fewer than clang -O0's 361,076), sp/fp operations 135,005 → 100,715 (−25 %; from start −42 %). Remaining: stores 11 %, loads 8.7 % (locals in inlined bodies and non-selected), spills around calls 3.5 % (up from 1.6 %: fewer callee-saved temporaries), saved callee-saved 5.9 %. Compile time unchanged (840 TUs `--batch -S` 7.6–7.7 s against 8.0–8.1 s). Verified: run tests t1–t6 (also `VCC_LREGS=10`), `bench/vcc-obj.py` 840/840 (same undefined symbols), `bench/vcc-fpcheck.py` 840/840, also with ASan/UBSan and TSan (TSan: two reports in `frontend/pp/include.c:428` and `arena.h:84`, not in codegen/), 838/838 kernel TUs from vcc boot Debian 13 to `running` (`results/boot-20261108-235047.log`, 28 stubbed symbols). `bench/vcc-kbuild.py` no longer builds the vDSO with vcc (requires PIC; a reused tree otherwise got vcc's `memcpy` calls in the vDSO) and aborts if kbuild rebuilt any vcc object with clang.
- **Performance (2026-11-08, loaded machine, load ~4):** the emitter on all 840 TUs: 459 ms CPU (0.55 ms/TU, of which printing 122 ms), 4.48 M instructions = 17.9 MB machine code, 39 MB/s, 103 ns/instruction, one thread per TU (`bench/vcc-emit-stats.py`). `kernel/sched/core.c` alone: 2.9 ms with 1 thread, 1.26 ms with 2, 1.04 ms with 4. Roofline (`bench/emit-roofline.c`, 29,755 instructions): machine code 0.10 ns/instruction (~40 GB/s), text 0.74 ns/instruction; we are two orders of magnitude above, mostly in generation and printing, not in memory bandwidth.
- **Optimization levels (2026-11-09):** `cc_opts.opt` gets the `-O` level (0: none/`-O0`, 1: `-O1`/`-Og`, 2: `-O2`/`-O3`/`-Os`/`-Oz`/`-O`). From `-O1`: register variables, inlined parameters and results in registers, removed jumps to the next label and `opt.c`; `-O0` is the simple code generator. The goal is that the rest of clang's `-O2` passes land at the same levels. `test/emit/run.sh` selects the level with `VCC_O` (default `-O2`). Verified: t1–t6 at `-O0`/`-O1`/`-O2`, `-O2` output identical to 74b2d82 (80/80 TUs), `bench/vcc-obj.py` 840/840, 838/838 kernel TUs from vcc boot (`results/boot-20261109-000308.log`).
- **Condition on a cset (2026-11-09, from `-O1`):** if the last record is `cset r, condition` for the value (`last_cset`), the flags are used instead of the value: a test in a jump becomes `b.condition` (`test_zero`), conversion to bool and widening integer cast become nothing, `!` and `== 0` invert the condition, `!= 0` keeps it. Covers `likely`/`unlikely` (`__builtin_expect(!!(x), …)` gave `cmp; cset; eor; cmp; cset; eor; sxtw; cbz`, now `cmp; b.cc`). Measured (80 TUs): instructions 291,677 → 268,502 (−7.9 %; 2.07× clang -O2), `cset` 11.6 k → 3.5 k, `eor` 7.5 k → 0.8 k; emit phase 76 → 74 ms. Comparison with clang per class and pattern: `ophist.py`/`ngram.py` (scratch). Next in order: arguments directly in x0–x7 (11.7 k `str` + 12.9 k `ldr`), spill only live temporaries, store→load forwarding on slots without address, redundant extensions. Verified: t1–t7 (new `t7_cond.c`; catches a mutation of the `== 0` inversion) at `-O0`/`-O1`/`-O2`, `bench/vcc-obj.py` 840/840, `bench/vcc-fpcheck.py` 840/840, 838/838 kernel TUs from vcc boot (`results/boot-20261109-001905.log`).
- **Arguments in registers, spill only live (2026-11-09, from `-O1`):** a scalar argument for x0–x7 that is in a temporary is no longer parked in a slot: `do_call` first classifies, keeps the temporary (neither parked nor spilled) and moves it with `mov` to its argument register (the moves write only x0–x7, never a temporary, so the order does not matter); the backward pass in `opt.c` then often computes the value directly in x0–x7. `spill_live`/`unspill` mark their records (`SPILL_REC` in `ins.sh`); `opt.c` removes the restore of a temporary that is dead after it and its store before the call. Measured (80 TUs): instructions 268,502 → 250,787 (arguments) → 240,941 (spill), −10.3 % in total (1.86× clang -O2); sp/fp operations 100,715 → 65,322; argument staging 11.7 k `str` + 12.9 k `ldr` → 0.7 k + 1.6 k; spills around calls 5.6 k `str` → 1.8 k. Emit phase unchanged (74 ms, measured alternately against b498617 on a loaded machine). Declined: letting a function state which registers it actually overwrites (like GCC's `-fipa-ra`) so that the caller saves fewer: `ftrace_caller` saves only x0–x8 and calls C code with `blr`, so with ftrace (and livepatch, BPF trampolines) x9–x15 would be overwritten; turned off in this configuration but a trap. Verified: t1–t7 at `-O0`/`-O1`/`-O2` (spill removal without a liveness check fails 6 of 7), file-by-file build without warnings, `bench/vcc-obj.py` 840/840, `bench/vcc-fpcheck.py` 840/840, 838/838 kernel TUs from vcc boot (`results/boot-20261109-002714.log`).
- **Stack slots: forwarding and dead stores (2026-11-09, from `-O1`):** slots are reused when a scope ends, so the emitter keeps a stack of live allocations and tags each FRAME record (`I_LD`/`I_ST`, address with `add`) with the allocation it is in (`SLOT_TAG` in `rm`/`ra`; an address at the start of an allocation that is also just after the end of the one below: `SLOT_TAG2`). Volatile accesses are not tagged. `opt.c` (`slots`): a cell is an interval in an allocation that all accesses reach with exactly the same `ldr`/`str` (no other interval in the allocation, no untagged access overlaps). Per block, a load whose value remains in a register (stored from there or loaded there, the register unwritten since) becomes `mov` or the load's extension of the register; a store that is overwritten in the block before a load is removed; a cell that is never loaded loses all its stores. If an address points into the allocation, forwarding happens only up to a call, inline asm or a store elsewhere, and no stores are removed. An untagged address turns the pass off for the function. Cells per allocation directly (only allocations with several intervals are sorted); registers read/written per record are cached from the liveness analysis (which also became faster: 655 → 520 µs on `core.c`). Measured (80 TUs): instructions 240,941 → 229,027 (−4.9 %; 1.77× clang -O2), sp/fp operations 65,322 → 49,134; per offset instead of per allocation gave only 235,257. Emit phase 74 → ~82 ms (alternately against 8f805df; ~0.1 ms per TU of ~90). Verified: t1–t8 (new `t8_slots.c`: narrow types, scope reuse, unions, address-taken locals that a called function writes, pointers just after the end backwards, varargs) at `-O0`/`-O1`/`-O2`; ignoring escape fails 7 of 8 tests, ignoring overlap fails t1 (the extension on forwarding is defensive: no test requires it, values in registers are already normalized); file-by-file build, `bench/vcc-obj.py` 840/840, `bench/vcc-fpcheck.py` 840/840 also with ASan/UBSan and TSan (TSan: only the known race in `arena.h`/`pp_open`), 838/838 kernel TUs from vcc boot (`results/boot-20261109-010043.log`).
- **Known extensions (2026-11-09, from `-O1`):** `opt.c` (`exts`) walks forwards over the blocks (meet at joins, fixpoint where only blocks whose in-state changed are revisited) and keeps per register z (the value fits in its low z bits, the rest zero) and s (it is the sign extension of its low s bits). Rules for loads (`ldrb`/`ldrsw`/…), `mov` constants, `cset`/`csetm`, `csel`, `ubfm`/`sbfm` (extension, `lsr`, `asr`, `lsl`), `and`/`orr`/`eor` (register and constant), `add`/`add #` (one bit is carried), `lsr`/`asr` with register, `udiv`, `clz`; every 32-bit operation zeroes the upper half. An extension (`ubfm`/`sbfm #0`, 32-bit `mov`, `and` with low mask) of a register that already has that form becomes `mov` x, which the copy cleanup then removes. Only x0–x15 and x19–x28 carry facts; calls and inline asm forget what they write. The function's entry, blocks nobody branches to and labels that inline asm (`asm goto`, trampolines) or a label address (`&&l`) can reach (new flag `LREF_EXT` in `lrefc`) start without facts. The answer per record is taken at the block's last visit (facts only become weaker), so no separate apply pass. Measured (80 TUs): instructions 229,027 → 218,331 (−4.7 %; 1.68× clang -O2); `ubfx` 7,657 → 1,082, `sxtw` 5,509 → 2,348 (the rest mostly after calls and for arguments, where AAPCS64 leaves the upper bits undefined; `ldrsw` 2,741 against clang's 110 comes from keeping int 64-bit sign-extended). The pass alone: 13.4 ms over 80 TUs with a separate apply pass and the meet bit by bit → 6.7 ms (~0.08 ms per TU of ~100). Verified: t1–t9 (new `t9_ext.c`: joins with different extension, loops that widen, `asm goto`, `&&l`, shifts, masks, 32-bit overflow, narrow call results, an asm function that leaves garbage in x0) at `-O0`/`-O1`/`-O2`; mutations that are caught: meet with min, ignored `LREF_EXT`, `add` without carry bit, `asr` of 32-bit unsigned, the mask's sign rule, facts kept across calls, `<` against `<=` in the `sbfm` rule; file-by-file build, `bench/vcc-obj.py` 840/840, `bench/vcc-fpcheck.py` 840/840 also with ASan/UBSan, `bench/vcc-cbatch.py` with TSan 840/840 (only the known races in `arena.h` and `include.c`/`enter_file`), 838/838 kernel TUs from vcc boot (`results/boot-20261109-093829.log`).
- **Copies, dead results and inline asm (2026-11-09, from `-O1`):** four generic changes in `opt.c`. (1) Labels that no branch reaches (scope, inlining) no longer split blocks: only labels that a branch record in the function points to or that have `LREF_EXT` begin a block, so all per-block passes see whole straight sequences (215,629 → 204,544). (2) Extended inline asm gets exact registers (`RAW_MASKS`: reads in-operands, `+` outputs, address registers for memory operands and x18/x29/x30; writes outputs and clobbers), stored after the asm text; simple asm and asm goto (its labels need what is live there) still read everything. Copy propagation never rewrites a register that the asm text names, `slots` counts every asm as a possible memory write, `exts` does not take asm for a 32-bit operation; `brk` reads and writes no registers (204,544 → 201,274). (3) Copy propagation also runs before `slots` (with recomputed read/write masks), so that forwarding sees the registers the values actually remain in, and after `exts`; `mov x, x` is removed (201,274 → 199,045, sp/fp operations 44,012 → 42,184). (4) Dead results: backwards per block from the block's live-out, pure instructions (ALU, `mov`, constants, addresses, loads from tagged stack slots; never flag-setting) whose rd is dead are removed, so that whole chains disappear (218,331 → 215,629). Total (80 TUs): 218,331 → 199,045 instructions (−8.8 %; 1.53× clang -O2), `mov` 50,934 → 39,408. `opt_function` 23.8 → 31.0 ms over 80 TUs (~0.09 ms per TU). Found and fixed along the way (own commit c4f2b10): callee-saved registers that inline asm clobbers were not saved in the prologue. Verified: t1–t10 (new `t10_asm.c`: outputs, `+r`, clobbers of x9–x15 and x19–x28, memory operands, fixed register variables, asm goto, labels in the middle of blocks, dead values, overflow builtins) at `-O0`/`-O1`/`-O2`; 11 mutations are all caught (among them copy into asm, without clobbers/outputs/inout/address registers, masks for asm goto, `slots`/`exts` without asm protection, without recomputed masks, labels without branches, self-copy in 32 bits); flag-setting instructions with a dead result and dead loads outside the stack cannot be provoked in the tests (the rules are defensive). File-by-file build, `bench/vcc-obj.py` 840/840, `bench/vcc-fpcheck.py` 840/840 also with ASan/UBSan, `bench/vcc-cbatch.py` with TSan 840/840 (only the known races), 838/838 kernel TUs from vcc boot (`results/boot-20261109-101030.log`).
- **Frame after the cleanup (2026-11-09, from `-O1`):** after `opt.c`, `opt_regs` recomputes what the records still use: callee-saved registers that the cleanup left unused (register variables, saved temporaries) are not saved, a frame where no slot is referenced any longer gets no locals, and a function without calls, slots, outgoing arguments, callee-saved registers, dynamic stack, over-alignment and without reads of x29/x30 (`__builtin_frame_address`/`_return_address`) gets no frame at all: just `ret` (flag 8 in `ctor` for the direct path). Like clang's omitted leaf frame pointer; inline asm that mentions x29 in the text then sees the caller's frame, just as with clang. Measured (80 TUs): 199,045 → 195,958 instructions (−1.6 %; 1.51× clang -O2). Verified: t1–t10 at `-O0`/`-O1`/`-O2`, ctests 1729/1729, abi (also `--host`), stress, bflayout, ldconst, fpcheck 840/840, `bench/vcc-obj.py` 840/840, file-by-file build, 838/838 kernel TUs from vcc boot (`results/boot-20261109-103809.log`). Remaining: leaf functions with locals (clang: only `sub sp`/`add sp`, no x29).
- **`opt.c` faster (2026-11-09):** same output, fewer instructions. (1) `roles`/`brkind` are looked up in tables per opcode (built once from the switches; `ldp`/`stp`/asm still go through the switch), `reads`/`writes` have an inline fast path (register bit from a table, one `csel` per operand; clang SLP-vectorized the branch-free variant to ~50 NEON instructions) and a slow path out of line for calls, asm and `ret`. (2) Liveness computes roles and masks once per record in the first loop and saves them (`W.ro`/`W.rr`/`W.ww`); the rewrites (forwards, backwards, `slots` load→register) keep them current, so the recomputation before `slots` disappeared and the copy propagation and `dead` read the cache (checked with a temporary comparison against recomputation after every pass: no deviations in 840 kernel TUs and ctests). (3) `exts` goes back directly to a back edge's target when its facts are weakened, so that what follows a loop is walked only when the loop is finished (2.25 visits per record before); functions without an extension that could be redundant skip the pass. (4) `slots` stops directly without tagged slot accesses. Measured (`bench/vbench.py ab emit`, 24 TUs): emit 633 → 575 M instructions (−9.2 %); without `opt.c` (`VCC_NOOPT=1`) 477 M, so the passes' share 156 → 98 M (−37 %). `gate` ok (emit −7.4 % against the merge base). Verified: `vbench.py check` 171/171 identical, all 840 kernel TUs' `.s`/`.o` byte-identical to the previous commit, t1–t10 at `-O0`/`-O2`, ctests 1729/1729, abi, stress, fpcheck 840/840, file-by-file build.
- **Two bugs from the optimizer (2026-11-09, reported by the review session):** (1) PostgreSQL `logging.c`: "internal: address of a register variable" at `va_copy`: Apple's `va_list` is a `char *` and can become a register variable, but `va_copy`/`va_end`/`va_arg` take its address; the prepass marked only `va_start`/`va_copy`'s target. Now all `va_list` operands are marked when `va_list` is scalar (Linux unchanged, kernel objects byte-identical). (2) `self-check`: step 2 segfaulted, `ppdriver.c`'s `obj_state` stored −1 in `tl_os`: `opt.c` took Darwin's TLV access (`I_TLV`, `blr x16` via the descriptor) for a call that writes x0–x18, so `mov x9, x28` before it looked dead; the thunk preserves everything except x0 (and x16, lr), which the emitter relies on. Now `I_TLV` writes only x0/x16/x30. Found by bisection per object (step 2 objects with and without `opt.c`) and then per function. Tests: `test/abi/va_copy`, `test/abi/tlv_regs` (catch both bugs with the previous commit, `--host`). Verified: `self-check` identical, `bench/pg/build.sh vcc` 1504 files, meson test 81/0, abi 7/7 and host 6 + 1 xfail, ctests 1729/1729, t1–t10, fpcheck, driver, stress, kernel objects byte-identical.
- **Remaining:** register cache for local variables (everything goes via stack slots today), the encoder instead of text (a64.c), PIC for the vDSO, floating point/VLA/TLS missing, value ranges for the `error()` calls that are now removed.

### Status 2.7 (2026-11-07): assembler for the kernel's `.S`, done (inline asm remaining)
- `src/as.c` (GNU as syntax), `a64.c`/`a64.h` (table-driven A64 encoder, shared with the emitter), `elf.c`/`elf.h` (in-memory ELF writer, mmap-reserved regions, no malloc; one object per thread). Driver `vasm` (`make -C src vcc`).
- Coverage after an inventory of the 30 `.S` TUs: `.macro`/`.irp`/`.irpc`/`.rept`/`.exitm`/`.purgem`, `.if*` as MC's AsmCond, `.pushsection`/`.popsection`/`.previous`/`.subsection`, data/`.inst`/`.incbin`/`.org`/`.fill`/`.align` family, `.set`/`.equ` with lazy variables (forward references, aliases inherit type/size), `.type`/`.size` expressions, numeric labels, `.req`, `ldr =` with literal pools, `:lo12:`/`:pg_hi21:`/`:abs_gN(_nc|_s):`. ~200 mnemonics (integer, ldst, system, SIMD, SHA, CRC, MOPS); 1,625 sysreg names taken once from llvm-mc (`gen-sysreg.sh` → `a64sysreg.inc`). No CFI is needed (the kernel builds without `.cfi_*` in these TUs).
- MC behaviours required for byte identity: section order (`.strtab` first, `.rela` directly after its section), StringTableBuilder's tail sharing, symbol order = registration order (mapping symbol before operand symbol), section symbols only if a reloc uses them, pc-relative fixups against local symbols in the same section are resolved, `$x`/`$d` per section but not for padding, nop padding with leading zeros in code.
- **Acceptance:** `bench/vas-diff.py cmp`: **30/30 `.o` byte-identical** to `clang -c`. `bench/vas-diff.py cmp --vpp` (our `vpp` instead of `clang -E`, i.e. no clang at all): **30/30**. For that, `vpp` now writes the source's whitespace in `asm_mode` (gas syntax is whitespace-sensitive: `\arg`, `661:`); the header cache is turned off in `asm_mode`, and C parity is unchanged (911/912). `bench/vas-enc.py` (every distinct instruction against `llvm-mc -show-encoding`): 2,266 ok, 0 failures, 524 cannot be taken standalone (local labels, `.req`) but are covered by the object comparison.
- **Speed** (one thread, input mmapped, load ~7): all 30 files 3.8 ms/pass: in 461 kB = 121 MB/s, out 123 kB = 32 MB/s, 26,902 statements after macro expansion = 7.1 M statements/s (162 MB/s expanded text). The critical path is `entry.S` (11,221 statements, 1.15 ms). Ceiling on the same machine: memcpy 33–65 GB/s, 4-byte store loop 12–13 GB/s, so we are 2–3 orders of magnitude below the ceiling. The cost is parsing per statement (~140 ns), not bytes, and it is evenly distributed (operand parsing, mnemonic lookup, encoding). The first profile was dominated by libc's `isalnum`/`tolower` (locale); a custom character class table and run copying gave 7.27 → 3.8 ms. As a process: 30 `vasm` in a row 102 ms (≈3.4 ms/file, i.e. the process floor), clang `-c` on the same text 1,413 ms. In parallel across files it is make that distributes; splitting a file into fragments does not pay off at 1 ms.
- Remaining: inline asm (operand substitution `%0`/`%[x]`/`%w`/`%x`/`%c`/`%l` and a snippet API on a persistent `As` bound to the emitter's `ElfObj`, see `assembler-interfaces.md` in the notes folder), integration in `vccd` for `-c X.S` (pp output directly to `as_run` in memory), CFI if the emitter needs `.cfi_*`.

### Status 2.8 (2026-11-07): linker + objcopy, `vmlinux` and `Image` without lld, boots
- `src/ld.c` (`vlink`, `ld_main()`): ELF linker for the kernel's two links. The final link with `vmlinux.lds` (script parser/evaluator with lld's ExprValue semantics, PROVIDE, orphan placement, merge sections in 32 xxh3 shards, AArch64 relocations incl. ADRP+ADD→ADR relaxation, thunks in lld's pass order, phdrs, `.rela.dyn` for `-shared -z notext`, symtab/strtab in lld's order, build-id) and `-r` (vmlinux.o for modpost). Inputs are mmap'ed, heavy phases are parallel (own thread pool, lock-free global table), output is built in one buffer and written with one write. `ld_main` takes a lock and can be called from vccd.
- `src/objcopy.c` (`vobjcopy`/`vnm`, symlinks to `vlink`): the build's objcopy/nm uses according to llvm-objcopy 23's ELF writer (segment-preserving layout, StringTableBuilder with tail sharing, symbol removal/prefix, `--prefix-alloc-sections`, `-O binary`, `-j`, `--dump-section`) and llvm-nm's BSD format (`-p`/`-n`/`-S`).
- **Acceptance:**
  - The final link is **byte-identical** to lld's `vmlinux.unstripped` (build-id included), with 1 and 8 threads. `-r`: `bench/vld-rcmp.py` against lld's vmlinux.o OK (92,429 symbols, 169,601 relocations); modpost gives identical output.
  - `bench/vobjcopy-cmp.sh`: 12/12 byte-identical to llvm-objcopy/llvm-nm (vmlinux, Image, modules.builtin.modinfo, vdso.so, 6 pi/*.pi.o, `nm -p -S vmlinux`, `nm vdso.so.dbg`).
  - The whole chain with only our tools (vlink → vnm → sorttable → vobjcopy → vobjcopy -O binary) gives an `Image` that is byte-identical to the lld build's; `bench/boot-test.py` with it: **BOOT OK, `running`, 0 failing units** (1.9 s).
- **Speed** (machine shared, load 2.5–5):
  - Final link: **30–34 ms** with 8 threads, 85 ms with 1 (lld 340 ms). Phases at 8 threads: mmap+parse 6–7 ms, resolve 3 ms, layout 3 ms, copy+relocate 2.9 ms, build-id 4.5 ms, write 4 ms. `-r`: ~14 ms.
  - Roofline: memcpy of 21 MB to newly mapped memory takes 1.5 ms with 8 threads (14.9 GB/s; 4.0 ms / 5.7 GB/s with 1), `write(2)` 3.4–7.8 ms. Copy+relocate reaches 7.3 GB/s ≈ 50 % of memcpy. The whole link: 29 MB in + 21 MB out in 31 ms ≈ 1.6 GB/s, about 5× the I/O floor (~6 ms). The rest is symbol resolution, parse and layout (~15 ms) and BLAKE3 (4.7 GB/s).
  - objcopy: strip of vmlinux 20.5 ms (llvm-objcopy 45 ms; floor for one thread ~12 ms), Image 9.3 ms (30 ms), `nm -p -S` 24 ms (61 ms). Process floor ~2 ms.
- Known differences: vnm's default sort orders entirely equal keys differently from llvm-nm (libc++ `std::sort` is unstable). The build does not use it.
- Remaining:
  - ~~the vdso link via lld~~, ~~`vlink`/`vobjcopy`/`vnm` in the build~~, ~~the link in vccd~~: done in 2.12.
  - `Image` can be written directly from vmlinux.unstripped in parallel with strip.

### Status 2.9 (2026-11-08): vccd compiles the kernel's `.c` and `.S` itself, objects without asm text
- `ppdriver.c`: `-c` of a `.c` runs pp (the header cache is shared between jobs) + cc.c + emit.c and produces an ELF object; `-c` of a `.S` runs the pp output through as.c in memory (`as_set_dir` for relative `.incbin`). One `As`/`ElfObj`/output buffer (4 GB NORESERVE) per thread, the object is written with one `write`, the `-MD`/`-MMD` depfile as clang's. `cc_run`'s tables are built once (`pthread_once`).
- `vccd.c`: `-c` of a named `.c`/`.S` with `--target=aarch64-*-linux*` runs in the worker thread (job type `cc-own`); host programs (Mach-O), the vDSO (`-mcmodel=tiny`, requires PIC), `-fpic`/`-shared`/stdin go to vroomd as before, and so does every TU vcc fails on (counted, logged with `VCC_LOG`). *Status 2026-11-10:* also stdin (`-`, read by vccd so that a fallback gets the same bytes) and host programs on an arm64 Mac (`-c` without `--target`; a `.c` straight to a program is compiled by vcc to a temporary object that the backend links, job type `cc-link`). The kernel build: 928 compiled by vcc, 0 fallbacks, 13 forwarded (6 host links of several objects, 5 `cc-link` links, 2 vDSO); vmlinux, Image, System.map, `.config` and all `.cmd` byte-identical with the build where clang built the host programs. `VCC_LOG=1` writes the whole command line of each forwarded job (`vccd forward:`). After that also the vDSO (`-mcmodel=tiny`: ADRP pairs are PC-relative like tiny's ADR; `__builtin_memcpy/memmove/memset` with constant size inline as clang's, the vDSO has no memcpy): 930 by vcc, 11 forwarded, all host links; no C file in the kernel goes to clang any more. vdso.so: the same five exported functions, no dynamic relocations, 5 440 bytes of code against clang's 2 664; Debian 13 boots (`running`, 0 failed units). `VCC_CC=0` forwards everything (A/B). Counters on SIGUSR1/SIGTERM. Thread stacks 16 MB.
- vas got what the kernel's inline asm uses: LSE atomics (cas/casp, ld<op>/st<op>, swp with a/l/b/h), ldar/stlr/ldapr, exclusive pairs, adc/sbc, smulh/umulh, umull/smull, `at`, SVE `ldr`/`str` of z/p with `mul vl`, pfalse/rdffr/wrffr/setffr, `:got:`/`:got_lo12:`, sysregs with op0 0–3.
- **Object backend** (`emit.c`): with `-c` the emitter's pieces are streams of records instead of text: directive text (section switches, `.globl`/`.type`/`.size`, top-level asm) goes to a persistent `As` (as.c split into `as_begin`/`as_snippet`/`as_end` + `as_sym`/`as_val`/`as_label`/`as_frag`); functions are encoded with a64.h's inline encoder directly into the current fragment; inline asm runs as a snippet in place; data objects and strings are written with `elf_bytes`/`elf_fill` and ABS64 fixups. The replay makes the same `ElfObj` calls the assembler made for the printed text, so the objects are byte-identical. Jumps in functions without inline asm are resolved in the encoder; with inline asm the `.LBB` labels are assembler symbols (asm goto can name them). `VCC_OBJ_TEXT=1` keeps the old path (printer + `as_run`) for checking.
- Bug found: the printer's `data_bytes` read 8 bytes past `e->data.n` for `.xword` and leaked old bytes from the previous object (`init_groups.ngroups` became 1; 19 TUs). Fixed; the kernel that booted in 2.6 had the bug.
- **Acceptance:** `bench/vcc-cbatch.py` (all TUs with `-c` in one `vpp` batch): object backend and `VCC_OBJ_TEXT=1` **byte-identical for all 839 distinct objects** (before the backend: the text path byte-identical with clang `-c` of the same text, `bench/vcc-asdiff.py`); `bench/vcc-obj.py cmp` **840/840**. Cold build through the shims (`bench/vccd-kbuild.sh`): **877 compiled by vcc, 0 to clang**; Debian boots to `running`, 0 failing units (`results/boot-20261108-075239.log`).
- **A/B** (`bench/vccd-kbuild.sh`, `-j 14`, alternating, load 10 → 44 from other things on the machine): compile phase (first → last CC) clang via vroomd 16.7/16.6/16.6 s → vcc **11.7/11.8/11.8 s**; total 22.1/22.1/22.1 → **17.0/17.1/17.2 s**. Before the object backend (text + as_run, one run): 20.6 → 13.9 s.
- **Where the time goes:** batch with `-j 6` (load ~4): 3.3 s wall, 16.9 s user + 3.1 s sys; `cc_run` 19.5 s job time, of which code generation + object **0.99 s** (text + as_run: 2.04 s), finish + write 0.25 s, 25.5 MB objects. The front end (pp + parse/sema) is ~90 %. (The brief's "~4.3 CPU-s" for the front end was wall time; CPU is ~20 s, cf. Status 2.4: 22.6 s.) `kernel/sched/core.c` alone: emit 2.5 ms against 3.3 ms (printing 1.0 → 0.25 ms). In the build the vcc jobs sum to 34.3 s (mean 39 ms, max 247 ms `net/core/sock.c`) over 11.7 s, that is ~3 jobs in flight out of 14: the compile phase is now limited by make, the shim and the dependency order, not by vccd (the clang jobs sum to 106 s).
- **Remaining:** the front end cost (declaration replay, RESEARCH-preprocessor §6.4); per-TU mmap/munmap in emit.c and cc.c (sys 3 s) → per-thread arenas reset per job; the vDSO (PIC) and host programs (Mach-O) still go to clang; why only ~3 jobs are in flight under make `-j 14`.

### Status 2.10 (2026-11-08): defconfig with modules, everything by vcc
- Configuration: defconfig with `CONFIG_MODULES=y` (1590 `=m`), without debug info and LTO; the build is `Image` + `modules`.
- **cc.c:** `overloadable` (Itanium mangling, clang's ranking); GNU vector types (`vector_size`, initializers, casts between vectors of equal size, `v[i]`, `+ - & | ^` and compound assignment); `__builtin_{add,sub}c*` and typed overflow builtins; `((` declarators; constant folding of address + integer, `strlen("…")`, scalar compound literals, float → integer; `__builtin_types_compatible_p` on qualified arrays.
- **emit.c:** `x && 0` / `x || 1` are folded (dead calls disappeared, modpost accepted modules); register variables without an initializer get their register (`fpsimd.c`, `w12`); `:lo12:` loads only when the object really is so aligned (ACPI's `*(u32 *)"SSDT"`); vectors in v16+, `"w"` asm operands, vectors as 16-byte records in vcc's own calling convention.
- **Own `src/headers/arm_neon.h`** (before clang's resource directory in the path): the ~25 intrinsics the kernel uses, as macros around vector operations and inline asm; run test on the host against clang with the real `arm_neon.h`: 34 000 cases, 0 errors.
- **vas:** SME `ldr/str za[…]`, `eor3`/`bcax`, NEON, AES, SHA512 and MTE instructions, `.octa`, `.reloc`, `.ltorg`, delayed expressions on label differences; the encodings checked against llvm-mc.
- Identifier table 2²³, shared pp region 16 GB.
- **Acceptance:** `Image` + `modules` build, **10 990 compiled by vcc, 0 to clang** (215.6 s with load from other things; earlier runs ~150 s). Suites green: parity 911/912, sema 878/879 + 878/879, obj 840/840, cbatch 840/840, vas 30/30.
- **Remaining:** boot test of the module kernel; time comparison against the clang baseline (575.6 s) on a quiet machine.

### Status 2.11 (2026-11-08): vcc compiles itself (macOS, Mach-O)
- `macho.c`: `ElfObj` → Mach-O `MH_OBJECT` (sections, symbols, ARM64_RELOC_* with ADDEND/SUBTRACTOR); `elf_finish` split into layout + fixups so that both formats share everything except the output. `ppdriver.c` writes Mach-O for Darwin targets.
- `emit.c`, Apple's arm64 ABI: `_` prefix, GOT for everything not defined in the TU, variadic arguments on the stack, packed stack scalars; floating point (bit patterns in GPRs, computation in v16/v17, arguments in v0–v7, constants folded on the host; float↔`__int128` via `__floatti*`/`__fix*ti`); `__atomic_*` (LSE, always seq_cst); TLV (`__thread_vars`/`__tlv_bootstrap`); VLA (allocated below sp like alloca, sp restored via the cleanup stack at block end, break, goto, return).
- `src/headers/`: own `stdatomic.h` and `arm_acle.h`; `arm_neon.h` is 2.10's (GNU vectors) extended with what vcc's lexer uses (cmeq/cmtst/cmhs, `vsriq_n_u8`; `vld4q_u8`, `vqtbl4q_u8` and `vshrn_n_u16` per lane because vas lacks ld4/tbl with four registers/shrn). The directory is put before the SDK on Darwin (`VCC_INCDIR`). cc.c: `?:` with two vectors of the same type (clang accepts).
- `make -C src self` compiles all 22 sources with vcc and links stage 2 (vpp, vccd, vasm, vlink, lextest, vcc) with ld64; `make -C src self-check` builds stage 3 with stage 2 and requires identical objects.
- **Acceptance:** stage 2 ↔ stage 3 **byte-identical (22/22 objects)**; stage 2 `vpp`: parity 911/912 (modpost.c as before), `vcc-sema.py run` 878/879, the kernel's 840 objects byte-identical with stage 1; stage 1 unchanged against the previous commit (0 of 840). `bench/vcc-diff.py` takes `VPP=` from the environment.
- **Time:** self-compilation 1.5 s (one thread, sequential). Stage 2 `vpp` is ~2.6× slower than the clang `-O3` build (kernel batch `-j 6`: 4.1 → 10.6 s wall) — vcc has no optimizer.
- **Remaining:** HFA arguments (fatal error), `long double` only as double; optimization in the emitter if stage 2 is to replace the clang build.

### Status 2.11 (2026-11-13) — native Linux x86-64 bootstrap

`make -C src -j6 self-check` builds the normal tools in parallel at each
stage, using vpp for every C object and the system compiler driver for linking.
Stage 2 and stage 3 produced 46 byte-identical objects and identical vpp
executables. The stage 2 compiler passed the native x86 suite (319/319, eight
reference skips), the x86 VIR execution harness and the 20 allocator fixtures.

Clean output directories, six make jobs, ccache disabled, warm filesystem,
one wall-time sample per configuration on x86-64 Linux with clang 23:

| Build of the tool suite | Seconds |
| --- | ---: |
| clang, default optimized build | 10.86 |
| clang, VCC_OPT=-O0 | 1.47 |
| clang-built vpp compiling stage 2 | 0.50 |
| self-built vpp compiling stage 3, including object comparison | 1.06 |

Before reusing the regular x86 objects for lextest and caching the clang
resource path, the corresponding optimized clang, stage 2 and stage 3 times
were 10.91, 0.79 and 1.24 seconds (57 objects). These timings cover compilation
and linking from existing compilers. That bootstrap used the scalar lexer and software CRC32C.
Native arm64 and Darwin still need verification with the shared build rules.

**x86 host SIMD and CRC32C (2026-11-13).** Both builds now assemble the same
AVX2 search routines and AVX2/AVX-512 VBMI byte classifiers, with byte-identical
machine code. The self-built binary selected AVX-512 and SSE4.2 CRC32C on a
Ryzen 7 9700X. Stage 2 and stage 3 matched across all 47 objects and vpp.
Dispatch parity passed under QEMU on Conroe, Nehalem and Haswell with and without
XSAVE. The x86 tiny-kernel preprocessor comparison passed all 494 C jobs. The
same seven assembly jobs failed before and after this change (four i386 jobs
and three SHA-256 assembly files).

SQLite 3.50.4 amalgamation (9,282,866 bytes, SHA256
`e3f5d6901e7492af4a1fc8c4d745cae84c264942524c3fbfc02b82a5ca8818c8`).
Median milliseconds across seven warm-cache runs, reversing order each round
after one warmup, pinned to CPU 7 with `VCC_THREADS=1`. Lexing uses
`lextest core LIST 3` with interning enabled (best thread CPU time of three
passes per invocation). Compilation times a fresh vpp process from launch
through object write, with `--target=x86_64-linux-gnu -O2 -c`, clang 23
resource headers and the host glibc headers.

| Executable and selected host paths | Lexer (ms) | Compile (ms) |
| --- | ---: | ---: |
| Self-built, scalar and software CRC | 82.1 | 536.5 |
| Self-built, AVX-512 and software CRC | 39.4 | 497.5 |
| Self-built, AVX2 and hardware CRC | 15.1 | 464.8 |
| Self-built, AVX-512 and hardware CRC | 14.4 | 465.6 |
| Clang 23 -O3-built, AVX-512 and hardware CRC | 6.4 | 238.3 |

All configurations produced token hash `fc9fb9fabfbd44b5` and object SHA256
`7766fa6e549b19c987d563f3c4f8f1217f6b6f1115659cdc646df2e1ccd97f54`.
The pre-change clang-built binary measured 6.2 and 235.7 ms in this series.
The self-built compiler still takes 1.95 times as long on the full compile.

### Status 2.12 (2026-11-10): kernel build without clang, vroomd and lld
Ported from the branch `claude/parallel-compiler-optimization-2bb32f` (2026-11-08) to main's layout.
- **vld, shared objects (vDSO):** `PHDRS` (FILEHDR/PHDRS), `VERSION` script (version-local → STB_LOCAL), `.dynsym`/`.dynstr`/`.hash`/`.gnu.hash`/`.gnu.version{,_d}`/`.dynamic` in lld's order, `=fill`, `-n`; only for `-shared` with a script. The vDSO byte-identical with ld.lld 23 (except the `$d` locals, known). Without `PHDRS`, `SIZEOF_HEADERS` is 64.
- **vld, GOT under `-shared`:** ADRP+LDR → ADRP+ADD (→ NOP+ADR) only when every ADR_GOT_PAGE in the section is followed by its LD64_GOT_LO12_NC (lld's safeToRelaxAdrpLdr) and the symbol cannot be preempted (`-Bsymbolic` or local). The kernel links vmlinux with `-shared -Bsymbolic --no-apply-dynamic-relocs`: without the relaxation the code read unrelocated GOT slots and a vld-linked kernel did not boot (empty console). `VCC_LDCMP=1` ctests 1773 → 1775/1775.
- **vld, several links in one process:** `reset_state` now clears the lazy archive tables (`lsyms`, `lcand`, `lout`, `ltaken`); before, they pointed into freed arenas, so vccd's second link with `--start-group lib.a` failed or crashed (present in main, first visible when vccd links itself).
- **vld, Mach-O (`platform/darwin/ldmacho.c`):** `ld_main` hands the link to `ld_macho` when the first input file is Mach-O: MH_EXECUTE as ld64 (`__PAGEZERO`, `__TEXT`, `__DATA_CONST,__got`, `__DATA`, `__LINKEDIT`), chained fixups, export trie, ad hoc signature (`ld_sha256` in ldhash.c). libSystem only.
- **vccd:** links in the thread through `ld_main` (`--vroom-stubs`; `--ld PROG`/`--lld PROG` start a linker instead), nm/objcopy through vnm/vobjcopy behind the `vcc-nm`/`vcc-objcopy` shims (one at a time), host programs (`scripts/`) are compiled by vcc for macOS and linked by `ld_macho`, `-` is read from the request's stdin (`pp_opts.main_file`). Without `--backend-sock` nothing falls back to clang: a kernel TU that vcc rejects fails with vcc's own diagnostics (`vjob_fail`/`vjob_escape` now write to the job's fd, not the server's stderr). With `--backend-sock` as before. `-S` writes the depfile like `-c`.
- **`bench/vccd-kbuild.sh`:** starts only vccd (no vroomd), sets `NM`/`OBJCOPY` to the shims, `VCCD_ARGS` to vccd.
- **Acceptance:** cold build exit 0, 9.9 s (kbuild.py; 11.1 s with the script's start/stop), load ~2: 884 compiled by vcc, 0 errors, 0 forwarded, 52 host jobs, 6 links, 14 nm/objcopy, 422 ar. `bench/boot-test.py`: BOOT OK (1.8 s, `results/boot-20261110-133025.log`). With `VCCD_ARGS="--ld contrib/phase1/vroom-ld"` (lld) the same tree also boots. vlink-opts 63/63, vlink-bad (ASan) 29/29, vccd-test 21 + 1 skip, vsh-test 37/37, vcc-stress --vccd 23/23, vccd-cc 174/174, var-cmp 12/12 + 111/111.
- **Remaining:** vld's Mach-O path lacks TLV, SUBTRACTOR and libraries other than libSystem; on Linux hosts the host programs do not yet go through vld (no ELF host link), so `--backend-sock` is needed there; vroomd is built but not needed by the kernel build. The config phase in kbuild.py still runs `ld.lld --version`.

### Plan 2.13 (from the branch, 2026-11-08): config in one step, declarations per header-cache entry
Measured on the branch before the lazy bodies (which main already has, see "Profile of vcc/vccd's own work"): the config step 0–2.6 s (allnoconfig, `merge_config.sh`, olddefconfig, `conf` twice), the main make before the first CC 2.6–4.3 s (`SYNC` = syncconfig + `conf` again and a restart of make), compile phase CPU-bound, tail 0.3 s.
1. **Config in one step** (`bench/kbuild.py` only): `make KCONFIG_ALLCONFIG=<fragment> allnoconfig` with the same tool variables (`LD`, `NM`, `OBJCOPY`, …) as the main make, so that `auto.conf.cmd` matches and the main make neither runs `SYNC` nor restarts. Measured once: 1.18 s instead of 2.6 s, `.config` byte-identical. Expected −2.5 s. Check: `.config` identical for `vroom.config`, `vroom-min.config` and the defconfig path; the build boots.
2. **Declarations per header-cache entry** (item 2 in the list under "Profile of vcc/vccd's own work"): the entry's delta (new entries per array, changes to older ones) and its dependencies (lookups outside the region with answers, also negative ones) are saved; a TU that reaches the entry at a declaration boundary with the same answers replays the delta, the preprocessor applies the entry without delivering tokens. Step 0 is measurement (share of tokens in entries at declaration boundaries, dependencies per entry, hits); build only at ≥ 60 %. Replaces the prefix tree (`ccsnap.c`). Check: objects and stderr identical with `VCC_NODC=1`, the suites, TSan on vccd-cc.

### Status test suites (2026-11-08): gcc c-torture + tcc tests2 imported

`src/test/gcc-torture/execute` (gcc e6bf7e6f, 1 701 tests, GPL-3) and
`src/test/tcc-tests2` (tinycc 70adbb12, 145 tests, LGPL-2.1), unchanged;
a README there on source, license, updating and why these (self-checking
run tests in GNU C, without dejagnu; gcc.dg/clang-lit are tied to their
diagnostics).
`bench/vcc-ctests.py -j 6`: each test is built by clang (oracle, cached) and by
`vpp -c`, linked with an own bare-metal runtime (`test/rt`: start.S, small libc,
built by clang) and run in `qemu-system-aarch64 -M virt` (semihosting gives the
exit code); vcc passes when output and exit code are clang's.  Cold ~2 min,
with cached references ~40 s.  Skips tcc-specific tests, ones clang
rejects and ones clang's build cannot handle on the runtime (long double without
compiler-rt, file I/O).

| | gcc | tcc |
|---|---|---|
| starting point | 1193/1589 (75.1 %) | 74/103 (71.8 %) |
| K&R, implicit int/function, `-std=` | 1306 (82.2 %) | 75 |
| main without return gives 0 (strcmp on uninterned name) | 1344 (84.6 %) | 78 |
| too long string start in char array | 1348 | 78 |
| SHF_MERGE reloc with addend against the symbol (as MC), `__builtin_expect` side effects, wchar_t without `-fshort-wchar` | **1353 (85.1 %)** | **80 (77.7 %)** |

Remaining failures (gcc+tcc): **fp 131** (floating point, own branch in progress), **fe 60**
(vector types 40, `__complex__`/`i` suffix 10, nested/unknown constructs), **emit 41**
(unknown builtin 55 = `__builtin_*_overflow` etc. 15, VLA 11, asm `g`
constraint 10, `.Ltmp` label 2, 128-bit switch), **link 14** (implicit `alloca`
should become a builtin 3, `link_error` test that requires constant folding 5,
`__sync_add_and_fetch`, `__udivti3`), **run 13** (VLA parameters' size expressions
with side effects 970217-1/pr77767, `__builtin_return_address`, computed goto in
comp-goto-1, va-arg-21, constructor order 108_constructor, 39_typedef, 129_scopes).
Next goal: builtins for overflow, VLA, asm `g`, then FP when that branch lands.
The predef table for Linux has the kernel's flags baked in (`-mgeneral-regs-only`
removes `__ARM_NEON`/`__ARM_FP`); should be adjusted when FP exists.

After merge with main (FP, vectors, overflow builtins, VLA from 2.10/2.11):
gcc 1453/1589 (91.4 %), tcc 90/103 (87.4 %).  After that: implicit `alloca` as
a builtin, pointers to VLA scale with the element's run-time size, `register` locals
without an initializer get N_DECL (the slot was allocated lazily inside a loop and
reused after `foff = mark`: 20000801-1/20000815-1 hung), `-fgnu89-inline`
→ **gcc 1463 (92.1 %), tcc 90 (87.4 %)**.  Remaining: fe 51 (vector operations,
`__complex__`), emit 45 (builtin 57/67, asm `g`, HFA arguments 9), fp 21
(floating-point va_arg 12, 128-bit floating point 6), link 8 (requires an optimizer:
`link_error` test), run 14.

Branch claude/vcc-ctests-3 (2026-11-08), one commit per step, all gates green
after each: the gaps in GNU vectors, floating-point va_arg (the v registers' save area,
`__vr_top`/`__vr_offs`), HFA arguments/results/va_arg (nested records and
arrays; over-aligned on the stack 16-aligned), `va_end` evaluates its
argument, C23 `[[...]]` attributes (`gnu::`/`clang::` become GNU attributes), old
`field: value` designators, `__sync_*`, isnan/isinf/signbit etc. at run time,
128-bit switch, K&R float parameters as double, compound literals live
to the end of the block (the slot was reused by later statements' temporaries), C99's
statement scope (if/switch/loops and their substatements are blocks), `destructor`,
no inlining of functions whose static local holds `&&label`, `_Complex`
(record `{ T re, im; }`, lowered in sema to the parts: N_CPLX; ABI as HFA/two words),
overflow builtins as statements (they were dropped before!), 128-bit overflow results,
VLA parameters' size expressions with side effects, bit-fields over 8 bytes, UTF-8 in
symbol names (vas), `__udivti3` etc. in the test runtime, qemu `-cpu max` (LSE).
→ **gcc 1572/1588 (99.0 %), tcc 99/101 (98.0 %)**, total 1671/1689 (98.9 %).

After that (same branch): `long double` as binary128 (soft float `__addtf3`,
`__multf3`, `__divtf3`, comparisons, conversions in the test runtime, integer
operations only; in the emitter register pairs, q registers in the ABI, HFAs of
long double, va_arg), `abs`/`labs`/`llabs` as builtins, `-finstrument-functions`,
label differences in static initializers, NRVO order for cleanup on return of a
local record (as clang), isnan/isinf (the left shift was a right shift),
libm functions on constants are folded, const floating-point globals are folded, and small
optimizations that the `link_error` tests require: shifts with 0/0 shift,
x op x and contradicting/complementary comparisons in &&/||, ranges for
`a || b` under a's negation, tracked constants in local array elements `a[K]`
that never escape, fabs-non-negative floating-point locals.
→ **1708/1708 (100 %)**, 136 skipped (clang rejects 69, clang's build
cannot handle the runtime 34, tcc-specific 32, compiler-specific 1).

### Status fuzzing (2026-11-08): lexer and preprocessor
- `src/test/fuzz/`: `fuzz-lex` and `fuzz-pp` (libFuzzer from Homebrew LLVM, ASan+UBSan; standalone driver without libFuzzer), corpus generated from kernel headers (`gen-corpus.py`, no kernel text checked in), dictionary `c.dict`, differential check against `clang -E -P` (`pp-diff.py`), regression cases that `make -C src fuzz-regress` replays. lex.c got `vlex_get_hook` (files from memory) and `VLEX_SCALAR` (scalar reference).
- Findings, fixed: (1) `vintern_copy` read 16 bytes from keyword and builtin literals (ASan, at start); (2) a fatal error in the main file (unbalanced `#if`) did `exit(1)` in `pp_open`, which would have killed vccd: now NULL; (3) `, ## __VA_ARGS__` removed the comma even when the variadic argument was empty but given (`VA(x,)`); clang does it only when it is omitted.
- Runs: 3 min × 2 workers per target; fuzz-lex without findings; fuzz-pp only false alarms from an own check (`#line 0` gives line 0, removed); findings (2) and (3) came from the seed corpus and pp-diff. A second round was interrupted (machine busy). Parity 911/912 (modpost.c) after each fix.
- **Remaining:** longer runs when the machine has free cores; pp-diff over the whole fuzz corpus.

### Status sanitizers (2026-11-08): ASan+UBSan and TSan, all findings fixed
- `make -C src vcc-asan` / `vcc-tsan` (`-O1 -g`, own `build/src-asan/`, `build/src-tsan/`; the normal build's commands unchanged). The harnesses take `--bin-dir DIR`/`VCC_BIN`; `bench/vccd-cc.py` runs 150 TUs `-c` through the shim against a private vccd. The large reservations (16 GB shared pp region, 4 GB NORESERVE) work without changes. AGENTS.md, "Sanitizers".
- Findings, fixed (one commit each):
  - ASan: keyword and builtin names were interned from unpadded literals (`vintern_cstr`; the fuzz session found the same); `pp_open` gave the caller's main file string (a malloc'ed job file in `vpp --batch`) to `vlex_get`, which reads 8 bytes past; lextest likewise. The contract is in lex.h.
  - TSan: `hc_mode` set the flag before `hc_mem_max` without ordering, so a thread could store in the header cache without a cap (now `pthread_once`), the same pattern in `dc_begin`; lazy env flags and `VPP_TIME` counters (`pp_stat_*`, `vdir_probes`) were plain `++` from all threads; `salloc` (the 16 GB region) and dircache `dalloc` read the base with a plain load after CAS; `file_line` wrote `nlines` non-atomically even for the loser; as.c `names_ops` "benign race".
  - After merge of the diagnostics work (ASan 7, TSan 19 failing TUs in vcc-cbatch, 731 TSan reports): `snap_restore` (the declaration cache) took the snapshot's `c->cases` (malloc) and `c->span`, so all TUs that restored the same snapshot realloc'ed the same block from several threads (heap corruption) and wrote spans into another TU's array; `sym.plb/ple` were not moved. Now own per TU, spans are a snapshot array with moved locations, and `cc_free` releases them (2 GB of address space leaked per TU). Objects byte-identical with `VCC_NODC=1`.
  - The header cache's lock-free CAS publication, the token cache, the intern table and vlink's thread pool gave no reports.
- **Acceptance** (after merge of main): ASan+UBSan (`UBSAN_OPTIONS=halt_on_error=1`) and TSan: `vcc-diff.py cmp --kernel` 873/873, `batch --cmp` 873/873, `vcc-sema.py run` 878/879, `vcc-cbatch.py` 840/840, `vccd-cc.py` 174/175 (the vDSO script goes to the backend), lextest `bench -p`/`idcheck`, vlink `-r` + final link and vobjcopy/vnm byte-identical with the normal build; 0 reports, no suppressions (`ccs/tsan.supp` not needed). Normal build: `vcc-diff.py cmp` 911/912, `vcc-sema.py run`/`layout` 878/879. TSan batch ~65–100 s, ASan 12–24 s per suite.
- **Remaining:** ASan does not see over-reads inside our own mmap arenas (unpoisoned); manual poisoning (`ASAN_POISON_MEMORY_REGION` behind the arenas' `used`) under `__has_feature(address_sanitizer)` would be the next step. The fuzz targets could also be built with TSan.

### Status PostgreSQL (2026-11-08): second project, macOS, meson + ninja, all C by vcc
- PostgreSQL 18.6 (`work/postgres`, REL_18_6, C) built natively on macOS with meson + ninja: `bench/pg/build.sh clang|vcc [setup|build|test|all]`. The reference is Homebrew LLVM 23 (Apple clang 21 answers differently, e.g. on `format(gnu_printf)`). `bench/pg/cc` is CC: `-c` of `.c` → `vpp`, compile+link (meson probes) → vpp per source and clang links, everything else (`--version`, `-E`, linking, `.S`) → clang. meson probes (`meson-private/`) are accepted only if clang also accepts them: vpp ignores unknown flags, and meson would otherwise add GCC flags (`-fpch-deps`, `-Wimplicit-fallthrough=3`). Installation to `work/pg-$V-inst` before test: SIP strips `DYLD_LIBRARY_PATH` when pg_regress runs psql via `/bin/sh`. Options: `-Dauto_features=disabled -Dcassert=true`, debugoptimized.
- Findings, fixed: (1) `__sync_val/bool_compare_and_swap`, `__sync_lock_release`, `__sync_synchronize` were missing (configure fell back to semaphores); (2) `__builtin_bzero`; (3) calls with more than 128 arguments (psql's sql_help.c: ~260) → arguments on the heap; (4) NEON intrinsics for simd.h/pg_popcount (`vqsubq_u8`, `vminq/vmaxq_u8`, `vcnt(q)_u8`, `vpaddlq_u8/u16`, `vpadalq_u32`, `vaddv*`, `vmaxvq_*`, `vceqq_u32`, `vzip1q_u8`, `vdupq_n_u32` etc.) and in vas `uqsub`, `umin`/`umax`, `cnt`, `uaddlp`/`uadalp`, `addp` (also scalar `addp d, v.2d`); (5) **local variables with alignment > 16** got only 16 (`PGIOAlignedBlock`, aligned(4096): Assert in mdextend): the prologue now does `sub x16, sp, #ram; and sp, x16, #-A`, locals/outs are rounded to A (ram addressed from x29, VLA/alloca, gives a fatal error instead); (6) **pp's tmp arena was reused while the parser held lookahead**: `string_lit` looks three tokens ahead, and a second `#x` overwrote the first's spelling (`CppAsString2('r') "," CppAsString2('p')` became `'p','p'`, psql `\dt` showed no tables); `-E` writes tokens directly, so parity never saw it. Now `pp_hold_tmp()` around `peek`; (7) **a declaration that is skipped** (`switch (k) { T *p; case …`) got its slot at first use, in the statement's scratch area, which a later call argument reused (ecpg's `check_indicator` segfaulted): `dead_decl` binds the slot at the declaration's place. (8) **`vpp --batch` hung** after ~1 470 of 1 574 TUs: the lexer's file cache (`ctab`, 65 536 slots, open addressing) and dircache (`dtab`, 16 384) never grew, and every probed path (also missing ones, times all `-I`) gets an entry; a full table = infinite probe loop in `centry_get`. Both are now lock-free chains (CAS on the bucket head), never full. Test `many_probes.c`: 65 536 `__has_include` in one TU (old vpp: hangs).
- Tests: `src/test/vcc/` (new suite in `vcc-ctests.py --suite vcc`, self-checking, one file per finding: 7/7, all 7 fail with vcc before the fixes) and `src/test/vas/simd-pg.s` (`bench/vas-enc.py $PWD/src/test/vas`: 47/47 against llvm-mc).
- **Acceptance:** pg_config.h identical to clang's except `USE_SVE_POPCNT_WITH_RUNTIME_CHECK` (vcc has no SVE types, the probe fails cleanly); all 1 504 C files by vcc without fallback; `meson test` 81 ok / 0 failed / 249 skipped (TAP off), same as clang's build (regress 231, isolation, ecpg, contrib, test modules). Build `-j 6`: clang 43 s, vcc ~26 s (via the Python shim, no vccd). The vcc suites: parity 911/912, `vcc-sema.py run`/`layout` 878/879, ctests 1715/1715, fpcheck 840/840, batch parity for the kernel 873/873.
- **Speed** (`bench/pg/speed.py -r 2`, 1 574 C files from the clang tree's compile_commands.json, `-j 6`, best of 2, load 10–16 on the machine):

  | mode | wall | CPU | files/s | vs clang |
  |---|---|---|---|---|
  | clang -O2 -g (the build's flags) | 45.2 s | 223 s | 35 | 1.0x |
  | clang -O0 -g0 | 26.1 s | 144 s | 60 | 1.7x |
  | vpp, one process per file | 11.3 s | 54 s | 139 | 4.0x |
  | vpp --batch (one process, 6 threads, as vccd) | 1.5 s | 5.3 s | 1 074 | 31x |
  Batch: 57 610 hits in the main cache, 935 restored declaration regions, 32 846 lazy bodies never parsed; 38 MB objects (clang -O0: 36 MB). 4 files fail in the vcc modes (3 × `pg_popcount_aarch64.c` with SVE from clang's configuration, `norm_test.c` lacks a generated header; it fails for clang too).
- **vccd in the PostgreSQL build** (`bench/pg/build.sh vccd`): CC is vcc's native shim against a private vccd. With `VCC_DELEGATE=bench/pg/cc` in the environment vccd itself compiles `-c` of `.c` (shared header and declaration caches) and runs everything else as `bench/pg/cc` (probes, linking, `--version`, TUs vcc fails on; `VCC_DELEGATE_SKIP=meson-private` always sends meson probes there). The driver now understands `-MQ`. Result: 1 504 of 1 504 C files by vcc in vccd, 0 fallback; `meson test` 81 ok / 0 failed / 249 skipped, as clang.
  - Full build (`ninja -j 6` after `ninja -t clean`): vccd 9–12 s, the Python shim ~26 s, clang 43 s.
  - psql only (`ninja src/bin/psql/psql` after clean, 214 steps, best of 2, load ~15): clang 4.4 s, the Python shim 2.95 s, vccd 1.06 s. In vccd the 145 compilations take 0.33 s wall (~10 ms each against 125 ms via the Python shim, where 50 ms per file was Python and the rest reparsed headers); what remains is genbki.pl (perl, 0.4–1 s, the same for clang) and the link tail (libpq.dylib, meson's `.symbols`, psql: ~0.3 s), which is not vcc's.
- **Profile of `vpp --batch`** (1 570 TUs, `sample` at -j 1, 2026-11-09): of busy time preprocessor ~47 %, parse + sema ~30 %, emit ~10 %, writing .o ~8 %. Fixed: (a) `find_include` was ~14 %: 175 probes per lookup, because the key lacked the path's id and every meson target (own `-I` first) put one entry per header under the same hash; now with `fsig`: 1.5 probes, same hits, -6 % cycles and -7 % user CPU at -j 6. (b) macho.c's qsort comparator read the object from a file-static variable: with several threads the symbol order was wrong in 36 of 1 570 objects; now `_Thread_local`, and -j 6 gives the same bytes as -j 1. (c) Failed `open()` of include candidates (~8 % of busy time): 78 644, all ENOENT, 73 % in directories that do not exist. dircache skipped only absolute paths (kbuild), meson gives relative `-I`; now the joined path is tried: 21 122 remain (in existing directories) against ~8 500 directory stats, sys time -25 % at -j 6, objects byte-equal. Safe because all build tree directories the include search looks in exist right after `meson setup`. The 21 122 that remain would require directory listing, and there generated headers may appear during the build: left alone. Remaining, in order of size: the creation of 1 570 object files (open+close+write ~7.6 %, path walk; `openat` against a saved directory fd would shorten it); parallel overhead (-j 6: 11 % more instructions than -j 1, more misses in the header cache when threads record the same header at the same time); the declaration cache reaches its cap of 256 MB (1 GB: -2 % instructions, +80 MB).
- Kernel: 22 of 839 objects change (vcc-cbatch, before against after): 9 of (5) (e.g. `call_single_data_t` on the stack is `__aligned(32)`), 13 of (7) (slot placement). Not boot-tested.
- **Remaining:** SVE (`arm_sve.h` types, `target("+sve")`); `__has_builtin` answers 0 for names that are not in the kernel/SDK (ppdata.inc, e.g. `__sync_lock_release`): generate with `EXTRA_INC=work/postgres/src/include`; diagnostics write vector types as `''`; the TAP tests (`-Dtap_tests=enabled`, IPC::Run); Mach-O linking (ld64 in the tail).

### Status directory structure (2026-11-09): ahead of x86-64
- `src/` in component directories (`lib/`, `frontend/lex/`, `frontend/pp/`, `frontend/c/`, `codegen/`, `as/`, `obj/`, `ld/`, `driver/`, `vccd/`), everything CPU-specific in `arch/a64/` (the emitter's 18 files, the as parser, the encoder, fixups, the linker's relocations and thunks), OS-specific in `platform/{linux,darwin}/` (the ELF and Mach-O writers), the Phase 1 tools in `contrib/phase1/`. Data structures in `lib/`: identical copies merged (`vreserve` ×4, `vregion` ×2, `vbuf` ×2), other variants side by side (TODO.md). Modules over several directories: Makefile lists `MOD_SRC`, still one unity TU per module (cc, pp, emit, as, obj). AGENTS.md "Layout" describes the structure.
- **Acceptance, every commit:** 1 326 outputs (`-c` -O0/-O1/-O2, `-S`, `-E`; kernel TUs, gcc-torture, tcc, vcc tests; Linux and Darwin targets) byte-identical with the build before; `vbench.py check --base` 171/171 (also the vmlinux link); vcc-ctests 1729/1729, parity 873/873, sema 878/879; gate within ±0.1 % in all steps.
- **Remaining:** the linker as a shared module (its arm64 parts are `.inc` fragments), the Darwin branches inside the emitter (`DARWIN(c)`) to `platform/`, common data structures. Already before: step 3 of `make self-check` crashes (stage 2 `vpp` segfaults), vasm lacks `ldursh` (vcc-asdiff 839/840).

## Linux aarch64 as host (started 2026-11-08)

**Goal:** the whole toolchain (vcc, vccd, vld, vas) and the kernel build run on Linux aarch64
with the same results as on macOS. Test environment: Debian 13 arm64 in Docker Desktop
(`env/linux/`), which on M1 runs a Virtualization.framework VM with native arm64, so
no emulation. The same image works in a qemu-system-aarch64 guest with `-accel hvf`.

**Status:**
- Done: `make -C src vcc` builds on both (Linux: `-D_GNU_SOURCE`, `-lm`, `-mcpu=apple-m1`
  on Apple cores, otherwise `-mcpu=native`, output in `build/linux/src`). vccd uses
  epoll + signalfd instead of kqueue. QoS, `sysctlbyname` and kperf (pmc.c) are
  behind `__APPLE__`; pmc is a stub on Linux (perf_event_open remains).
  `ppdata.inc` had a non-numeric line (`__has_cpp_attribute(clang::…)`), which
  gen-ppdata.sh now filters out.
- The harnesses find LLVM and the build directory via `bench/hostenv.py`.
  `env/linux/run.sh CMD` runs in the container, and `--seed-work` copies the kernel tree to
  the volume `vroom-work` (Docker Desktop cannot share the work.sparsebundle mount).
- Verified in the container against the Mac tree (build-o0): ctests 1706/1706 (2 more ref-fail
  than on Mac: clang 23.1.6 against 23.1.4) and kernel parity 873/873. The Mac tree's host TUs
  need the macOS SDK, so they are tested in the Linux tree below instead.
- Performance (kernel/sched/core.o, -fsyntax-only, best of 5): macOS 115 ms, container
  138 ms with `-march=armv8.4-a`, 125–128 ms with `-mcpu=apple-m1`. The remaining ~10 % is
  probably 4K pages (linuxkit) against 16K on macOS, plus glibc malloc/memcpy.
  ctests: container 79 s, macOS 105 s.

- Linux build tree (2026-11-08): `work/build-linux` in the volume, built with clang 23.1.6 and
  env/vroom.config (after `olddefconfig`, which only adds CC_CAN_LINK and RELR).
  Command: `env/linux/run.sh python3 bench/kbuild.py --dotconfig env/vroom.config
  --out /vroom/work/build-linux -j 8`, which takes 97 s with plain clang. The references are in
  `work/ppref-linux`. The harnesses choose the tree via `bench/hostenv.py` (KTREE/PPREF/ASREF).
- vcc without `--target` on Linux means hosted aarch64 Linux, that is glibc's
  paths and clang's resource directory (VCC_RESDIR, set at build time). PIE and CFI are on by
  default. NEON macros are defined without `-mgeneral-regs-only`, which also fixes
  kernel TUs and .S that lack the flag. `-fpic`/`-fpie` cancel each other as in clang.
- gen-ppdata.sh takes `EXTRA_INC` (a copy of glibc's /usr/include in `work/glibc-include`),
  which gave 213 new nonzero answers (`__builtin_*f128`, `__builtin_stdc_*`,
  `__has_extension(__attribute_deprecated_with_message__)`, ...). Numeric answers with
  L suffix are kept.
- I/O: sources, build trees and references are on ext4 (the volume vroom-work), and
  `build/linux` on the volume vroom-build. `/tmp` is tmpfs. Only the repo's source files are read over
  virtiofs. ctests: 79 s over virtiofs, 64 s on ext4.
- Verified on Linux: kernel and host parity 920/920, vcc-sema run and layout 887/887
  (median 94 ms), ctests 1706/1706, vcc-diag 63/63.

- **vccd kernel build on Linux (2026-11-08):** `env/linux/run.sh sh bench/vccd-kbuild.sh
  /vroom/work/build-vccd -j 8` (work/linux-pf, also in the volume). vroomd is built against
  Debian's LLVM 23 (libclang-cpp23-dev). Tool paths on Linux come from
  `VROOM_LLVM_BIN` (Makefile: the LLVM directory of `clang` on PATH) in vccd, vroomd and
  vroom-cc. vroom-ld looks on PATH. caps.c answers `-print-file-name=include` with
  VCC_RESDIR. Fixed: vroomd's children inherited `SIGCHLD = SIG_IGN`, so Linux reaped the
  driver's cc1/ld itself and `waitpid` gave ECHILD; the child now restores SIG_DFL.
  Before the merge of main (3ec10bf: ccsnap-restored TUs get their own span and case tables)
  the build on Linux gave random "duplicate case value" and a glibc malloc assert, and on
  macOS nothing was noticed. glibc's allocator reuses freed memory faster.
  - Result: **877 compiled by vcc, 0 to clang** (two runs). Cold build
    **8.2–8.3 s** (user 6 s, sys 2 s). The same code and tree on macOS with -j 8: 12.5–13.9 s
    (user 9–10 s, sys 17–18 s, load ~4 from other sessions). The difference is
    the system time: process spawn and file syscalls are cheaper in the Linux VM. The Image from
    Linux **boots Debian to `running`, 0 failing units** (`results/boot-20261108-184214.log`).
  - The kernel objects are byte-identical with the Mac build, except the version string (`7.4.0-rc1` against
    `-rc1+`: .git is missing in the volume) and the vDSO (clang 23.1.6 against 23.1.4 via vroomd).

**Next:**
1. Profile the build on Linux (perf exists; pmc.c via perf_event_open), now that
   the system time does not dominate.
2. Measure a 16K-page kernel (own VM kernel) against linuxkit's 4K.
3. vroom-build.sh (zsh, perl for time) portable, and the shim static without libc init on
   Linux.

## Shared optimizer without SSA

### Decision and code state (2026-11-11)

Implementation plan after the x86 audit in `PLAN-x86.md` X4. This section
owns the shared optimizations; the x86 plan owns the targets' instructions,
ABI, encoding and wiring. No new optimization is implemented by this
plan. G5's quality goal and G7's compile time must be measured separately.

The code review was done after the rebase on main `4daebd9`; the audit's numbers
refer explicitly to its older base `52261ef`. The next code step builds and measures
a new base before any performance comparison. The old results are not rewritten.

- `codegen/tu.c` chooses between three generators. `codegen/walk/` is used only by x86;
  `walk.h` has a hardcoded `LO(name) x64_##name`. `wkarg` and its
  SSE/eightbyte classes are also SysV-specific. The directory's name therefore
  does not mean that arm64/wasm already use it.
- `codegen/prepass.c` already has `luse`, `lnoreg`, `lspan`, `sflags`,
  `fnsize`, write and address-escape facts. Reuse them, with
  conservative supplementation where they do not prove what a new rule needs.
- arm64's `choose_lregs`/`bind_lreg` keep selected scalars in registers.
  `arch/a64/codegen/opt.c` has blocks/liveness, copies, stack cells, known
  extensions and dead results. `finish_function` chooses the frame and saved
  registers after the cleanup. This is existing functionality to extract,
  not a new optimizer to write once more for x86.
- x86's `wk_arith`, `wk_decl`, `wk_const_if`, `wk_can_inline` contain
  semantic rules; `x64_finish` runs only `x64_twoaddr` and the printer.
  `rec.h` has a 16-byte format and `opdesc`, but no wired-in
  shared pass engine or complete x86 op table.
- Wasm already keeps suitable scalars in wasm locals. Its operand stack
  and structured control flow do not require physical register liveness.
  Share C rules, analyses and strategies with wasm; do not force its
  values into a register machine's representation to call the code shared.
  Concrete connections: `wexpr.c` has `wbinary`/`warith`, `wknown`,
  `wlocal` and `wvlanes`; `wstmt.c` has `wswitch`, `wlabels`, `wterm` and
  structured branches including `br_table`; `wcall.c:wcall` has the calls.
  These already overlap x86/arm64 in semantics, but not in value representation.

### Boundaries between shared and target

| Shared responsibility | Proposed place | Target's responsibility |
| --- | --- | --- |
| Type-correct algebra rules, the constant division's parameters | `codegen/opt/identity.c`, `divpow2.c` and separate rule files | Shift/mask/mulhi/div, immediate limits and the cost of a sequence |
| Reachability, preserved side effects, local facts | `codegen/opt/flow.c`, existing `prepass.c` | How branch/label/return is emitted |
| Inlining's eligibility and budget | `codegen/opt/inline.c` | Arguments/results, registers, ABI and target constraints |
| Selection of locals that can live outside memory | `codegen/opt/locals.c` | Physical registers or wasm locals, callee-save, prologue and spill |
| The switch's sorting, density and strategy choice | `codegen/opt/switch.c` | Tree branches, code/value tables, relocations or wasm `br_table` |
| Block boundaries, copies, dead results, stack cells, extension facts | `codegen/rec/` | Op descriptions, fact transfer for target instructions, safe rewrites |
| The address's base/index/scale/displacement and type width | Shared address description at `lval`/walk | x86 SIB/LEA, arm64's index/offset forms, wasm address computation |
| Semantics of explicit vector operations and fallback | Shared rule at the vector expression | SSE2/NEON/wasm SIMD, target features, alignment and ABI |
| Tail calls: semantic admissibility | Shared return/call decision | Stack layout, ABI compatibility and epilogue + jump |

`codegen/opt/` is pure emitter helpers, not a new graph/IR or a new
optimization step over the whole AST. Small decisions/sequence descriptions may be
returned without allocation. They take types, flags and explicit call context;
no modification of the shared AST, `ccsnap` or function bodies that several
threads/inlining instances use. The rules must not know x86 opcodes,
register numbers or `DARWIN`. The target adaptation must not copy the rules.

**Three consumers are the delivery requirement.** A general integer/flow rule
is not done when it is called only from x86. Wire it in and test it in
x86, arm64 and both wasm32/wasm64 for types the target supports. A missing
wasm tool is reported as unverified, not as a passed step.
The register machine's postpass requires two consumers (x86 and arm64); wasm
instead has the same semantic rule and its own structured lowering.

The existing AST and the machine records are kept. `ins` and `rec` have the same
field positions but different meaning: arm64's `sz` is not x86's byte width,
and stores use different register fields. Use explicit adapters; make
no aliasing cast between struct types and do not assume the same semantics
because both are 16 bytes. `emit_SRC` in `src/Makefile` needs to include new
`codegen/opt/*.c` and `codegen/rec/*.c`; every file must work without unity too.

### Order, deliverables and verification

OPT0–OPT4 have delivered sub-steps with verification and cost measurements below;
OPT5's shared cell/value core is in main (PR46), while the wasm adapter
is in PR47. Each row describes the whole remaining deliverable, not a promise
that the whole step is already done. The status blocks state the actual scope.
Continue with small separate commits and updated status/measurement here.

| Step | Deliverable and dependency | Proof before done |
| --- | --- | --- |
| OPT0 | New built base after rebase; share probe expectations in C semantics and per-target code requirements. Extend the audit's callers with existing arm64/wasm runners without requiring that all references exist in CI. | Existing x86 result reproduced; available targets run the same defined input. Separate results for opt flag and target. No old measurement is given a new revision. |
| OPT1 | Extract and wire in shared integer rules in all three generators: `+0`, `-0`, `*1`, bit identities, unsigned `/1`, `/2^k`, `%2^k`. Reuse existing folding and normalization. Requires OPT0. | Semantic tests on x86/arm64/wasm; targeted shift/mask requirements where the target supports them, negative tests for volatile and side effects. Unchanged code for examples without the rule. |
| OPT2 | Shared effect and post contracts, complete op table for x86 and adapter for arm64. Extract block identification/compaction from arm64; the first generic cleanup takes only safe fallthrough jumps and dead code after a terminator. Requires OPT0. | Arm64's extraction byte-identical; x86's unnecessary jumps/dead arms disappear; asm goto, `&&label`, switch/case in if(0), noreturn, VLA/cleanup and debug remapping correct. |
| OPT3 | Move the candidate selection out of `choose_lregs` to a shared `locals.c`, connect to x86's bindings. Limited top-K by existing loop-weighted `luse`; the target gives K/registers. Wasm reuses the eligibility rules, not an artificial register limit. Requires OPT0 and the effect contract in OPT2. | Parameters/locals stop going through the stack in the probes; mixed calls, recursion, fixed asm registers, varargs, returns_twice, over-aligned frame, VLA and register pressure. Arm64's original choice and output identical in the extraction commit. |
| OPT4 | Reuse arm64's copy/dead-result/extension algorithms in `codegen/rec/`, first per block on x86. Target tables describe register overlap, flags and rewrites. OPT2 required; OPT3 gives a larger code gain but is not a requirement. | Positive and negative code requirements; no errors across calls/asm/block boundaries. A 32-bit self-move on x86 is removed only if the upper half is already zero. FP and implicit registers/flags preserved. |
| OPT5 | **In progress: shared cell/value core; wasm4/8-byte forwarding/overwritten-store DSE implemented, x86 cost-optimized.** Extract arm64's stack cell algorithm and connect x86 via explicit allocation/access metadata. Reuse the same algorithm for load forwarding and DSE. Requires OPT2/OPT4; choose exact non-escaped frame cells first. | Arm64's t8/t10 properties ported to C regressions: scope reuse, overlap/union, narrow types, address escape, pointer just after object, calls/asm/atomics. Volatile reads/writes kept. |
| OPT6 | Shared switch choice and extended inlining eligibility. Separate commits: first balanced switch trees and value tables; then small common static functions and unused local initializers. Requires OPT1/OPT2; the inlining's stack traffic motivates OPT3/OPT5 first. | Dense/sparse/signed/range cases, overflow in the density computation and computed goto. Inlining: recursion, labels, VLA, cleanup, asm immediates and side effects. An unused init is removed only when both the object and the init lack observable effect. |
| OPT7 | Shared description for scaled addresses, direct explicit vector operation and store-constant hook with scalar/register fallback. Then small target-specific rules for LEA/SIB, NEON/SSE2 and instruction features. Requires OPT1; metadata from OPT2 follows along. | Same C result on the targets; target requirements show indexed load/packed add/store-immediate. Negative alignment, width, alias, lane and feature cases; baseline must never get native instructions by mistake. |
| OPT8 | Shared constant division algorithm for signed powers of two and other constants; target-lowered mulhi/shift or normal div according to the target budget. Limited tail calls with shared check and ABI hook in its own commit. Requires OPT1 and, for tail calls, OPT2/OPT3. | Division/remainder 32/64 bits, negative numbers, extreme values, random and limits around multiples. Tail calls: no pointers to own frame, stack arguments, cleanup or ABI conversions without explicit support. |

**First code commit:** OPT0 + extraction of a single integer rule in OPT1,
called by x86, arm64 and wasm32/wasm64 for supported types. It
shows the sharing in actual use before more rules are added. OPT2
can be prepared after that; the register work OPT3 has higher priority than
more aggressive inlining. No large move of the whole arm64 generator is required
to begin sharing rules or postpass.

### OPT5 status: shared cell analysis (2026-11-11)

`codegen/rec/slots.c` and `slots.h` now contain arm64's analysis of exact
allocation cells, overlap and address escape. Arm64 uses it directly;
register versions, liveness and the load/DSE rewrite itself remain
for now in its adapter. No new function walk or separate
post/effect table has been added. Scratch capacity is reused per thread.
The allocation ID separates lifetimes at the same offset. ID 0 denotes an
unmarked/observable access and blocks all overlapping cells, even if
no load of the cell survives. Both escape IDs at one-past are kept.
Invalid widths/ranges are rejected before offset arithmetic.

Verified on base main9d022c3: unity and file-by-file build; 177/177 own
arm64 run tests. 6069/6069 object/text results are byte-identical against
eddce37 (2023 files × O1 object/O2 object/O2 text; its arm64 output was
unchanged by the copy extraction in PR43). A separate ASan/UBSan contract
compares the analysis with a simple interval model over 400 deterministic
48-access cases, plus explicit lifetime, boundary, volatile and escape cases.
The contract runs in CI. A second agent has reviewed the extraction.

Delimited cost measurement: both compilers built with clang 23,
`-O3 -march=x86-64-v3`, Callgrind 3.26, whole `vpp -O2 -S` to arm64,
`VCC_LREGS=0` to really load the slot analysis. Base 9d022c3 and
candidate 36fe740, 500 functions per corpus:

| Corpus | Base, Ir | Candidate, Ir | Difference |
| --- | ---: | ---: | ---: |
| summation loop | 35 284 633 | 35 515 725 | +0.655 % |
| straight integer code | 17 330 749 | 17 383 341 | +0.303 % |

The assembler output is byte-identical for both probes. Raw profiles, binary hashes,
inputs and the measurement command are in `work/opt-profile/cells-main-*`,
`cells-main-profile.json` and `cells-measure.py`. These two instrumented
probes are no general approval of G7 or a hardware cycle measurement.

Next code step: move value availability and DSE decisions to a shared
engine with the targets' existing read/write/block data; x86 must first carry
stable allocation IDs and volatile/atomic facts from the walker. Wasm
needs stable value IDs and structured memory operations before
text emission. OPT5, G5/G7 and the whole O2 goal remain open.

### OPT5 status: shared value and store facts (2026-11-11)

`codegen/rec/slot-values.c` now shares arm64's value availability and
the decisions for overwritten/unread stores. The target owns value ID and
write versions: a native register or a stable wasm local fits in the
same uint32 contract. Block generations prevent forwarding across
joins without analysis; global remaining loads are counted over all
blocks. Escape also requires the same memory version, and prevents DSE.
No SSA or new post/effect table. The existing scratch for value facts
and load counters has been merged into one reused per-cell table.

On the eeac13c base: 6108/6108 arm64 objects/text byte-identical (2036 files,
O1 object/O2 object/O2 text), 190/190 own run tests and a new stack_cells
run case. Isolated ASan/UBSan contracts test block boundaries, source clobbers,
memory versions, read/forwarded loads, large unsigned value IDs
and reused capacity; a second agent has reviewed the adapter move.
X86/wasm adapters and cost measurement of the combination are in progress in separate
working copies; this is still partial progress in OPT5, not full O2 coverage.

### OPT5 value core: check against merged main (2026-11-11)

Base f866a31 now contains PR43 and PR45. Unity and file-by-file build;
6117/6117 arm64 objects/text are identical against the base (2039 files ×
O1 object/O2 object/O2 text). Own arm64 suite 193/193, including the new
stack_cells; the new run case also passes with the file-by-file binary.
The cell/value contracts run sanitized. The portable regression has
also been verified O0–O3 on x86, arm64, wasm32 and wasm64 (16 runs).

Callgrind 3.26, whole `vpp --target=aarch64-linux-gnu -O2 -S`,
`VCC_LREGS=0`, identical clang23 `-O3 -march=x86-64-v3` builds:

| 500-function corpus | Base f866a31, Ir | Value core e6bad4c, Ir | Difference |
| --- | ---: | ---: | ---: |
| summation loop | 35 612 350 | 35 511 064 | −0.284 % |
| straight integer code | 17 431 966 | 17 400 666 | −0.180 % |

The assembler output is identical in both probes. Raw data, binary hashes, sources and
run command are in `work/opt-profile/values-main-*`,
`values-main-profile.json`, `values-measure.py` and `{loops,control}-500.c`.
This measures the refactoring's instruction work on two delimited probes;
G5/G7 still require broader code quality/performance gates.
X86's metadata cost and wasm's typed events are not part of this measurement.

### Contracts that govern the implementation

- **Wasm integration:** the arithmetic helper returns a typed decision,
  not a `val` with a physical register. `wbinary` can choose the decision before
  the operand stack is filled; if an operand has already been emitted the adapter code must
  preserve the right stack depth and use drop/local where needed. Shared
  fact bindings are per function/inlining instance. Wasm inlining requires
  own parameter locals, structured result/return block handling and
  remapped goto/cleanup/VLA scope; shared policy alone is no inliner.
  `wswitch` reuses the strategy choice but keeps `br_table` and the
  segment/goto model. Wasm32 normalizes pointers as 32 bits even on
  this 64-bit host. Explicit SIMD requires a target feature; without it the
  lane fallback is kept. Magic division without a cheap mulhi and tail call without
  an enabled/supported wasm tail-call feature keep the ordinary div/call.
- **Types and effects:** the first arithmetic rules apply to ordinary integers
  up to 64 bits; `_BitInt`, 128 bits, FP and pointers have explicit
  fallback until their rule is verified. Use the operation's promoted
  width and signedness; signed division rounds toward zero. The power-of-two
  check must exclude zero. No FP identity or fast-math rule
  is smuggled in. Operands with calls, volatile or atomics are evaluated exactly
  as before even when the result becomes constant. Value-level rules after
  operand generation keep effects; skipping at AST level requires
  a separate effect proof. Recurring effect questions are cached per instance or
  taken out of the prepass, not recursive walks with quadratic cost.
- **Registers:** shared selection is policy, register binding is target mechanics.
  Start as arm64 with a few callee-saved GPRs for non-address-escaped,
  non-volatile/atomic integers and pointers. x86 must reserve them from
  the temporary pool and handle r12 as frame base and asm clobbers. Used
  callee-saved registers are saved/restored according to the target ABI, including
  unwind/debug. Retry is at most one conservative one without local registers;
  remaining pressure uses the existing spill/error path. FP and register pairs
  are later separate extensions. Do not at the same time add a competing
  general register cache on top of the bindings.
  Wasm also keeps its already supported floating-point locals; shared
  eligibility rules are parameterized by the target's type support and must not turn
  the register machines' first GPR limitation into a wasm regression.
- **Post effects:** the tables need reads/writes including implicit
  registers, flags/partial flag writes, memory read/write,
  volatile/atomic/order, trap/terminator/barrier, stack change and
  external label entries. Unknown instruction/asm is conservative. A load
  can be observable or throw an error even if the result register is dead.
  Start DCE with pure register operations and proven safe frame reads.
  Calls/TLS, the x87 stack and x86's subregisters require their own descriptions.
- **Memory identity:** do not reuse slot offset as object identity.
  Keep a stable allocation id, range/width, access kind and escape;
  the byte offset is 64 bits after main's frame changes. Overlap and
  one-past addresses are handled conservatively as arm64 already does. Prefer
  lazy side metadata in the opt mode over making every 16-byte record larger;
  do not replace arm64's tags until the replacement is verified byte for byte.
  Side metadata must follow insertion/compaction/twoaddr and debug lines.
- **Pass cost:** opcode table on the fast path, target callback only when a
  real rewrite or a deviating instruction requires it. Buffers are
  reused per worker, without locks and without allocation per node/record.
  Do not run a pass without relevant instructions/cells. Constant-size
  register facts give O(n); top-K is O(locals*K) with a small fixed K;
  switch sorting O(k log k) and table size have a cap. Magic division
  gets O(bit width) per new constant and a limited cache, not a search.
- **CFG budget:** no global fixpoint analyses are required for the first x86
  passes. At block end unknown live-out are conservatively live, not
  dead. Later shared CFG liveness gets a separate work budget; if it runs
  out, safe facts/output without dependent rewrites are used. Unfinished
  liveness must never be accepted as correct. Arm64's existing analyses
  are kept until a shared replacement gives at least the same correctness and measured
  cost; a local variant must not silently degrade its existing O1.
- **Code order:** semantic rules at AST lowering; shared record passes on
  valid effects; x86 two-address after shared passes; target-specific cleanup;
  final register/frame decision and encoding. If two-address introduces new copies
  a delimited post-cleanup suffices, no general fixpoint. Debug/CFI
  must not refer to old record indexes or removed saves.

### File structure for optimizations

Each delimited optimization or tightly connected rule family gets a
named file: now `codegen/opt/identity.c` and `codegen/opt/divpow2.c`.
Shared postpasses get corresponding files under `codegen/rec/`. Do not put the
algorithms in a growing `walk.c` or expression walker. The walkers and
backends keep small calls/adapters for operand evaluation, types,
registers and the wasm stack. Avoid splitting a small connected
rule into one file per operator. The file-by-file build verifies the boundaries.

### Levels, test requirements and measurement

`-O0` may get cheap direct lowering rules without starting dataflow passes.
Which rules are default is measured, not assumed. O1 is the budgeted
shared improvements above. O2/O3 honestly remain the same until a
separate, measured policy exists; do not just change macros or level names
to claim Clang parity. LICM, global GVN, automatic vectorization,
loop unrolling/unswitching and general IPA come after the first series
OPT0–OPT8, but are part of the continued goal of covering Clang/GCC O2 on
all three backends. Create IR/SSA first for the analyses that need it;
every later pass must have a measured CPU budget. Limited
block-local CSE can be added after OPT4 when its benefit justifies a
fixed-size value cache; no whole-function search or general alias analysis.

Each semantic rule gets C regressions in `test/vcc` that run on all
supported targets. Positive code requirements are architecture-specific; negative checks
for effects are shared. Upstream GCC's C cases are a supplement, not a
replacement for checking their `dg-final`. Add a small vcc-only
regression run in `ci/premerge.sh` for targets where it can run; the
interactive three-compiler audit must not become CI's dependency on native
GCC, GNU objdump or all optional runners.

Pure extraction commits require byte-identical output against a before-base on
affected targets, unity and non-unity. When the rule is activated only the
intended code differences may arise: run suites/ABI/stress for affected targets,
arm64's slot/asm/extension regressions, and the CFI/DWARF/kernel or
host checks that the change actually touches. Parser/pp are not changed
for emitter optimizations. If they must be changed anyway, AGENTS.md's
sema/parity gates apply before the change. G1/G2 are not completed by this plan.

Measure before/after with short runs and at most `-j 6`: the compiler's
instructions/cycles and memory, generated code size/instructions and
real run time on a fixed small corpus. Report O0/O1 and per target,
including excluded cases and missing tools. The existing `vbench.py gate`
applies; until its x86 support is confirmed it must not be stated as a run
x86 gate. Do not approve any imagined pass budget as a measured figure. Save
base revision, binary hash and measurement results at each finished OPT step here.

### Status OPT0/OPT1 (2026-11-11): first shared arithmetic rule

New base built on `990c9fe` (main code from `4daebd9`); vpp SHA256
`b1d0c06a74de9695c8c04e67993c81e686d91d946afd8f32ecea169843af91f3`.
The base's own x86 suite passes 130/130, five filtered cases. The base was saved as
`build/linux/opt-baseline/vpp` before the change.

`codegen/opt/divpow2.c:em_divpow2` now owns a shared rule for unsigned
ordinary integers up to 64 bits: division by constant 2^k becomes a logical
right shift and remainder becomes AND with 2^k−1. The divisor is normalized to
the operation's promoted type before the test; zero, signed, pointer, FP,
vectors, _BitInt and wider integers fall back. No allocation,
AST modification, SSA or pass loop; the rule applies at O0 too.

Connections: arm64's central `arith`, the x86 walker's `wk_arith`, and
wasm's `wbinary`/`wassign`. Binary expressions and scalar `/=` and `%=`
are covered. The register targets' operands are already evaluated; wasm replaces only
a provably effect-free constant RHS and keeps the other evaluation and
stack types. Vector lanes with dynamic representation and other algebra
identities are not new deliverables in this step.

`test/vcc/unsigned_pow2.c` tests literal-based answers, 32/64-bit
max values/high bit, divisor 1, truncated/signed-converted constants,
signed/non-power/dynamic control cases, volatile effects, calls,
comma RHS and compound assignments. `bench/vcc-opt-rules.py` audits
the functions separately at O0–O3 and can run without libc on all four targets.
Wasm uses vpp's own C→object path and vlink, not Emscripten; see
`PLAN-wasm.md` W9. A .s file through vpp -c does not today choose the wasm
assembler, so the test compiles the same C at the same level to its own object.
This is a known driver gap, not an LLVM fallback in the test.

Candidate SHA256 before documentation
`77c05982e3bc056476aeeac6cc02c3309cba10026db19ecbd5e3a846b1dde440`.
Short interleaved measurement on Ryzen 7 9700X: three repetitions, eight
6000-function TUs, serial batch, warm=1, O1 -S, without header/declcache.
Median of the sum of the job times (ms, base→candidate):

| Target | Power-of-two arithmetic | Unchanged control corpus |
| --- | --- | --- |
| x86-64 | 44.370 → 37.864 | 36.940 → 36.176 |
| arm64 | 57.737 → 54.005 | 53.619 → 53.747 |
| wasm64 | 34.475 → 35.131 | 32.468 → 32.616 |

This is wall time in short warm jobs, not hardware instructions or
proof of a 1 % gate: perf is blocked (perf_event_paranoid=4) and
Linux vperf gives zero. No system setting changed. Small differences are
noise; the measurement shows no large control regression. Sources, commands,
raw data and paired results are in `build/linux/opt-perf/paired.json`.
Two minimal code examples, unsigned /8 and %8: x86 16→12 instructions
each; arm64 11→10 for division and 12→10 for remainder; wasm32/64 get
shr_u/and instead of div_u/rem_u with the same number of stack operations.
No improvement in the program's run time is claimed as measured here.

Verified: the candidate's x86 own suite 130/130 (the same five filtered cases
as the base), arm64 own suite 135/135 via QEMU, with -j 6 and --no-cache.
Unity and full file-by-file build green; both build modes pass all
16 combinations of four targets and O0–O3 in the code/run harness. The system's
Node 24.22.2 and then 25.9.0 pass wasm32/64 without an SDK path.
The new code gate rejects the before-base's division on all four targets, so
it detects the optimization gap itself in addition to the correct C result.
Syntax check of Python and shell and `git diff --check` green.
GHA hooks are added but no remote CI or full Emscripten-based
wasm suite has been run in this step.

OPT1 is thereby **started**, not fully done: the identities and other
OPT steps remain. The Emscripten-independent wasm test path is delivered separately
from the replacement of the full runtime/explorer. G5/G7 remain open.

### Status OPT1 (2026-11-11): separate files and integer identities

The power-of-two rule has moved unchanged to `codegen/opt/divpow2.c`.
`codegen/opt/identity.c` now owns type-checked RHS rules for +0, −0,
*0/*1, /1, %1, &0/&all-ones, |0/|all-ones, ^0 and shifts by zero.
All three generators use the same recipe. Operand effects are kept;
32-bit register results are also normalized after lazy truncation.
FP, pointers, vectors, _BitInt and wider integers are not covered.
No allocation, new IR or analysis loop is introduced.

Base: `e4c1c9a`, vpp SHA256
`77c05982e3bc056476aeeac6cc02c3309cba10026db19ecbd5e3a846b1dde440`.
Candidate before documentation:
`f3899f1f5e51ff895d15903f547797bf6096ba16c8f2016f00aa005e1c70178f`.
Verified unity and file by file: 32 code/run combinations (two rules,
four targets, O0–O3), without Emscripten. The system's Node 26.13.0 runs wasm.
X86 own suite 131/131 (the same five filtered cases), arm64 136/136 via QEMU,
-j 6 and --no-cache. Regressions cover narrow types, high bits after
truncation, signed extreme values, volatile and exactly one side effect.

A short paired compiler measurement is in
`build/linux/opt-identity-perf/paired.json`. Large host-time noise and
blocked hardware counters prevent conclusions about small CPU differences;
no 1 % gate or program run-time gain is claimed. Minimal /1 functions
shrink from 16→12 instructions on x86, 11→10 on arm64 and 9→7 on
wasm32/64. The control corpus and raw data were saved separately.

OPT1's identities and unsigned powers of two are delivered.  This does not
complete OPT2–OPT8 or the whole O2 goal; the next step is a separate shared
fallthrough cleanup with preserved debug remapping.

### Status OPT2 (2026-11-11): shared fallthrough cleanup

`codegen/rec/fallthrough.c` now shares arm64's existing removal of
unconditional jumps to the directly following sequence of local labels.
X86 runs the same pass at O1 and above before the two-address conversion.
The post-layout is described by stride/field offset; the label is read with
memcpy so that arm64's `ins` is never type-punned to x86's `rec`. Asm and
named labels are barriers. One linear forward sweep, no allocation or
fixpoint. Later removals can expose earlier jumps; the cascade cases are
still run-tested but are not promised to be removed in this step.

Wasm's existing conservative `wlabels`/`wterm` has been given its own file,
`arch/wasm/codegen/reachability.c`. While no longer emits an extra
loop branch after a body that provably cannot fall through. An explicit
continue keeps its branch and cleanup; do-conditions and for-increments
are preserved. This is structured target lowering, not an artificial
machine-record postpass on wasm.

Base `f7ccaff`, vpp SHA256
`f3899f1f5e51ff895d15903f547797bf6096ba16c8f2016f00aa005e1c70178f`;
candidate before documentation
`384cfd5f6c83fba8852dbc9ff0c1e7ad3ce323a489519164d304d55fcc8b36eb`.
Arm64's 131 existing compilable own-suite files give byte-identical
O1 -S with -g and unwind directives against the base; five earlier rejections
are unchanged. The two new test files were excluded from that comparison
because they were still being edited when the base was taken. Both build
modes pass 64 target/level/rule combinations, with target-relevant code
requirements and running on all four targets. X86 own suite 133/133 (the
same five filtered), arm64 138/138, -j 6 --no-cache. O1 DWARF verification,
unchanged .text with -g and direct object against assembler output are green
for both new test files. Existing CI invocations include the new rules
automatically.

Generated code at O1, number of unconditional branches in each new test file:
x86 `branch_fallthrough.c` 31→21, arm64 20→20 (pure extraction);
wasm32/64 `while_backedge.c` 44→38. Run semantics cover backward jumps,
label sequences, switch fallthrough, side effects, asm barrier, goto into/out
of a loop, cleanup and continue.

Short paired measurements (three repetitions, four 3000-function TUs,
-j 1 --warm 1, O1 -S) with base/candidate hashes and raw data are in
`build/linux/opt-flow-baseline/paired.json`; a targeted remeasurement is in
`recheck.json`. The first medians for the flow corpus in ms were x86
25.586→25.436, arm64 29.313→29.784, wasm32 24.381→28.059 and wasm64
22.922→22.644. The control corpus is saved separately. The remeasurement varies
strongly, especially wasm32; no small CPU regression or gain can be
established and the 1 % gate is not proven. Hardware counters remain
blocked; no system settings were changed. Program run-time gain not measured.

OPT2 is only **started**: full effect description, block contract and
dead code after a terminator remain. Register locals (OPT3) and the remaining
OPT steps/the O2 goal are still open.

### Status OPT2 (2026-11-11): unreachable records and x86 effects

`codegen/rec/unreachable.c` removes, in a linear, allocation-free sweep, the
records after a safe unconditional transfer up to the next possible
entry point. Arm64's adapter is separate, in
`arch/a64/codegen/unreachable.c`; x86 has a cheap one-byte flow table in
`arch/x64/codegen/effects.c`. O1 and above, with the existing noopt guard.
All labels, raw asm, named labels, unknown instructions and
indirect transfers are conservative barriers. Debug remapping runs for
each old record index and the end marker. Wasm's structured dead code
already uses the corresponding AST proof; its semantics are also run-tested.

Arm64 BRK is **not** a safe terminator: debugger/arm_trap can resume.
The review also found that the shared side_effects lacked DEBUGTRAP;
it is now classed as an effect so an unused debugtrap call is kept.
The regressions check both breakpoint and continuation without
executing these compile-only functions.

X86's full register/effect contract for upcoming copy/liveness passes
is in the same target-specific effects.c. It describes implicit registers,
flags, partial writes, TLS, REP, x87, FP environment, R11 scratch and R12 as
adjusted frame base. Unknown/missing metadata becomes opaque, never pure.
`bench/vcc-rec-effects.py` tests these contracts normally and with
ASan/UBSan; the normal fast test is part of arm64's premerge job.
No full register analysis is started for simple flow cleanup.

`unreachable_records.c` tests goto/return/break/continue, reachable effects,
volatile, computed goto, asm goto, asm barriers, cleanup and VLA.
Code requirements reject the base's x86 code; literal-based run results are kept on
all four targets at O0–O3. The audit's x86 probe `oa_cond_fold` goes at O1/O2
from 16 to 11 instructions. Both build modes and the relevant DWARF tests
are verified together with the following signed power-of-two rule; full
measurement data and revision hashes are reported at OPT8 below.

OPT2 is still **started**: shared block/effect adapters for the whole
arm64 optimizer and full CFG reachability remain. This is not general
SimplifyCFG. The next larger step is register locals with shared eligibility
(OPT3), then copy/extension cleanup (OPT4).

### Status OPT8 (2026-11-11): signed powers of two with target cost

`codegen/opt/sdivpow2.c` describes a shared rule for signed division and
remainder with positive/negative constant powers of two up to 64 bits,
including the minimum divisor and −1. A bias before the arithmetic shift gives C's
rounding toward zero. Remainders are computed from the low-bit mask and the same
bias. The magnitude is computed in i128 to avoid overflow in the compiler.
Separate `sdivpow2.c` files in arm64, walk and wasm own the lowering; the calls in the
expression walkers are small. No new analysis, allocation, SSA or AST mutation.
_BitInt, wider integers, pointers, FP and vectors are not covered.

**Wasm's cost choice:** a direct comparison with local clang 23 -O2 shows
that LLVM leaves /8 and %8 as a single div_s/rem_s. Therefore wasm does
not expand the long bias sequence. It uses the same recipe for cheap /−1,
%−1 and quotient with MIN divisor (equality comparison, i64 extended from
i32). Other signed powers of two keep compact wasm code for the engine.
This is a target decision, not missing C semantics. A pure constant RHS is still
required in wasm; an effectful comma RHS is evaluated normally.

Tests: `signed_pow2.c` covers 32/64 bits, both divisor signs, shifts
1/3/4/30/31/62/63, extreme values, −1 (excluding MIN/−1 which is C UB),
narrow promoted types, lazy truncation with garbage in the high bits,
compound/volatile, exactly one side effect and nested temporaries.
Reference results are computed with volatile dynamic divisors, so the test
compares the machine's divide path against the new constant lowering.

Verification for OPT2/OPT8 together: unity and file by file build without
new warnings; both pass all 96 target/level/rule combinations with
code requirements where relevant and running on x86, arm64, wasm32 and wasm64.
X86 own suite 135/135 (five previously filtered), arm64 140/140, -j 6 and
--no-cache. O1 DWARF tests for new flow/division cases green; metadata
contracts tested with ASan/UBSan. No remote CI or full wasm runtime
with Emscripten has been run. Neither G5/G7 nor the whole O2 goal is done.

Measurement base for the series: `98da0c3`, vpp SHA256
`384cfd5f6c83fba8852dbc9ff0c1e7ad3ce323a489519164d304d55fcc8b36eb`;
final candidate before documentation
`294cc2a1264fdee3a21c413e826d1ab5062b12a4076c51178882022ed21691a9`.
`build/linux/opt-next-baseline/paired.json` contains three alternating
repetitions, four 3000-function TUs per batch, O1 -S, -j 1 warm=1.
Besides job time, the child process's user+sys CPU time is saved, including
warmup but without waiting for other processes' scheduling. CPU time is not
hardware cycles and also varies with the host's frequency/cache/memory load.
Medians for the signed corpus in ms (base→candidate): x86 46.668→53.825,
arm64 46.220→52.126, wasm32 46.286→41.464, wasm64 40.145→48.886.
The flow corpus and the unchanged control corpus and all raw repetitions are saved
separately. Native bias expansion generates more simple records and has a
measurable cost in the deliberately division-dense microcorpus. No
1 % gate or general absence of compiler regression is claimed.
The earlier wasm bias expansion was rejected in favor of compact code.

The three-compiler audit before/after is in the same directory as
`optaudit-before/` and `optaudit-after/`. All 12 baseline combinations
have the same checksum `1922a5c83358247b`. The x86 signed /8 probe has at
O1 the same number of instructions (14) but divide goes 1→0; the unreachable arm in
`oa_cond_fold` goes 16→11 instructions. The audit after is from the candidate
before the separate debugtrap fix; the final focused verification
covers the fix. Program run-time gain is not measured.

OPT8 is **started**: other constant division (mulhi), a shared
cost model, and tail calls remain. A continued full O2 plan also requires
loop/alias/global analyses after OPT0–OPT8. OPT3 should now reduce
stack/copy traffic before more advanced passes.

**Preparation for OPT3:** code review shows that x86 can use
RBX/R13–R15 for locals; R12 belongs to adjusted frames. Shared selection
reuses lspan/luse/lnoreg. Existing L_VREG can be used, but a read
must copy to an owned temporary and normalize narrow types. All
potentially promoted callee-save registers must also be saved by
builtin setjmp even when the setjmp function itself does not choose them: otherwise
a longjmp past a promoted intermediate frame can destroy the caller's locals.
This and asm clobbers/inline-instance boundaries are requirements before activation.

### Status OPT3 (2026-11-11): shared selection and x86 register locals

`codegen/opt/locals.c` now owns shared eligibility and a stable weighted top-K
with the existing lspan/luse/lnoreg. No new AST walk, graph or
allocation. Arm64 keeps its physical register choice and wasm its
representation requirements, including FP/i128/SIMD locals. Shared exceptions
are address escape, volatile, atomic, VLA, cleanup and fixed asm registers.
The prepass's address marking now also follows the comma RHS like the emitters'
lvalue lowering. Parser and preprocessor are unchanged.

X86's target binding is in the separate `arch/x64/codegen/locals.c`: at most four
homes in RBX/R13/R14/R15, O1+, !noopt, with the VCC_LREGS budget (0 turns it off).
R12 belongs to adjusted frames. L_VREG is read into an owned temporary; a narrow
read uses a direct X_EXT from the home, so high bits are normalized
without an extra copy. The homes' original values are saved in ordinary frame slots
before the parameter binding and restored at the shared return label.
Stack arguments' offsets are not changed. Parameters still go through their
existing ABI slots and are then read into the home once. Inline copies
use separate memory bindings (inst != 0), no aliasing of homes.

**Cheap profitability:** x86 requires at least four loop-weighted references and
excludes locals that are already constant-bound without storage. At most four such
checks, no second symbol/AST sweep. Filtered candidates are not
backfilled yet. This avoids extra ABI spills in cold simple functions;
the audit now shows unchanged code for all cold probes. The loop functions
have more static instructions for prologue/epilogue but less dynamic
stack traffic. Direct ABI→home and copy cleanup are the next possibilities.

Asm requires no TU-global register barrier: the existing asm lowering saves
all operand/clobber registers and restores them on each asm-goto edge
before output writes. Four simultaneously live homes are tested against real
clobbers and both taken/fallthrough asm goto. No repeated asm scans
or global disabling of unrelated functions is introduced.

**Nonlocal jumps:** a separate `arch/x64/codegen/setjmp.c` always saves all
non-frame callee-save registers RBX/R12/R13/R14/R15, including inherited values from
the caller when the destination itself has no promoted locals. Public
builtin buffer layout unchanged. An X_RESUME_FRAME record recreates R12
as align_down(RBP−8, final falign) before private snapshot loads.
The writer uses literal LEA/AND so a normal memory rewrite does not change
the base. An unaligned frame emits nothing. This also supports alignment that
is discovered after setjmp or in an inline body; the earlier local
rejection can be removed. The regression case jumps past another frame
with alignment 256. Metadata describes the pseudo-record's registers/flags.

Verified: unity and file by file without new warnings; 116 applicable
rules/levels/targets per build mode (32 x86, 28 each for arm64/wasm32/wasm64),
all with running at O0–O3 and bounded code requirements. After the aligned-resume
change the four relevant nonlocal cases were rerun for both build modes.
X86 own suite 137/137 (five previously filtered), arm64 142/142, -j 6
--no-cache. X86 cross-ABI clang/vcc 22/22, O1 DWARF for new test files 2/2,
metadata contract with ASan/UBSan green. The base is rejected by the new register
code requirements and the nonlocal regression; clang passes the nonlocal case O0–O3.
Arm64 extraction: 135/135 compilable existing test files byte-identical
with O1 -g/unwind; wasm32 114 and wasm64 117 byte-identical. All older
rejections unchanged. Input hashes and results in snapshot JSON.

CI now installs qemu-user and runs x86's libc-free optimization suite from
the arm64 job; the QEMU version is part of the tool fingerprint. The harness picks
qemu-x86_64 on another host and can force the emulator locally with --qemu-x86.
All x86 cases have also been tried with this prefix path, including the final
nonlocal cases. No remote GHA run and no full wasm runtime replaced here.

Base `eddfc69`, vpp SHA256
`294cc2a1264fdee3a21c413e826d1ab5062b12a4076c51178882022ed21691a9`.
Measurement candidate before the aligned-resume addition:
`645d2d05f88339aa11d316bb85fbd1eefba1e354f9f30ff8b47dc63ff51d8c75`;
final candidate before documentation:
`3b399256f3b795cc5267d72394cfdd4dfc9b3a271fc3c30f38ff1dbaef202ade`.
Artifacts in `build/linux/opt-locals-baseline/`, including before/after audit,
snapshots, compiler measurement and runtime.py/runtime.json.

Compiler CPU (child's user+sys, three alternating repetitions, four
3000-function TUs per batch, O1 -S, -j 1 warm=1):

| Target | Loop corpus ms, base→candidate | Unchanged control corpus ms |
| --- | --- | --- |
| x86-64 | 55.650 → 61.701 | 24.630 → 25.112 |
| arm64 | 88.148 → 88.696 | 33.659 → 32.337 |
| wasm32 | 47.222 → 45.502 | 22.401 → 22.011 |
| wasm64 | 43.927 → 43.046 | 22.156 → 22.139 |

The native loop corpus costs about 11 % more compiler CPU in this synthetic
measurement; cold code remains unchanged but the selection itself also costs time.
No 1 % gate or general freedom from regression is proven. Hardware
counters are missing; the shared host's frequency/cache/memory load affects CPU time.
The measurement candidate above is from before the last, rarely used resume support.

Short native program test, unsigned sum of 4096 elements called 2000
times via an extern function (no LTO, identical checksum 4096177840),
three alternating user+sys samples including process start: vcc O1 base
7.830 ms → candidate 6.345 ms (about 19 % shorter). Clang O2 0.778 ms,
GCC O2 3.330 ms on the same corpus still show a large quality gap.
This is a small loop test, not a general program speedup. The audit's
sum function goes from 33→43 static instructions, stack references 14→12
(ABI spills included); reduced loop traffic pays for the prologue/epilogue here.

OPT3's first part is delivered, but direct parameter binding, better
profitability selection and OPT4's copy/extension cleanup remain. Full O2
coverage, general SSA/alias/loop analyses and G5/G7 are still open.


### Status OPT3 (2026-11-11): direct ABI binding

X86 now binds selected scalar register parameters directly from the ABI register
with declared width/signedness. Selected stack parameters are loaded once
from the caller's existing offset. The separate post-load hook and
useless parameter slots are removed. The existing classification still owns
the GP/FP budget, aggregate rollback, hidden sret and varargs;
no new analysis, AST walk or allocation.

Unity and file by file build; the parameter cases run O0–O3 on x86, arm64,
wasm32 and wasm64. The code requirements reject the base's spill/load. New cases cover
narrow signed/unsigned types, register/stack arguments, mixed FP/GP,
24-byte sret and varargs. X86 cross-ABI 22/22; nonlocal regression O0–O3; x86 own suite
137/137 with five unchanged filterings (--no-cache, -j 6).

Base d8e02c8 SHA256
`3b399256f3b795cc5267d72394cfdd4dfc9b3a271fc3c30f38ff1dbaef202ade`;
candidate `08fa144ebfd7166f9e5b499b6f209413a6ba131cf9374064b5d5c0816744a20c`.
Artifacts `build/linux/opt-params-baseline/`. Same method as the previous
OPT3 measurement, three alternating repetitions: x86 loop compiler CPU
60.282→58.437 ms, control 24.806→25.012 ms. The other backends' source is
unchanged; their measured times vary within the host's noise. The same small sum
program gives 3.263→3.057 ms, identical checksum; clang O2 0.508 ms and
GCC O2 1.793 ms. Times from different measurement occasions must not be compared directly.
No general speedup or approved 1 % gate is claimed.

Direct binding is delivered. Better selection, copy/extension cleanup,
memory optimizations and full O2 coverage remain; no SSA introduced.

### Status OPT4 (2026-11-11): shared forward copy cleanup

`codegen/rec/copies.c` now holds the shared forward search from
arm64's copy pass. Post-layout, effects, liveness and allowed operands
come from the target; the rewrite is prechecked before anything is changed.
At most 256 user records are kept in a thread-local workspace. No new AST walk
or SSA is built. Arm64 reuses existing block/liveness data and
debug mapping. X86's adapter in `arch/x64/codegen/copies.c` does one effects pass,
local backward liveness and compaction with a reused thread-local workspace.
It is enabled from O1 for 64-bit GPR copies to temporaries, within a
straight block and with proven local overwrite. Calls, asm, atomics,
block boundaries, implicit/two-address operands and partial writes
stop unsafe rewrites. 32-bit copies and XMM are not handled yet.
Wasm has no such machine records; its code generation is not changed here.

Unity and file by file build. `bench/vcc-rec-copies.py` passes normally
and with ASan/UBSan, including negative boundary/width/register cases and a
transactional boundary at 256 rewrites. `--rule copies --run` passes
O0–O3 on x86, arm64, wasm32 and wasm64 in both builds (32 combinations).
The base is rejected by the new x86 code requirement for `cp_stores` at O1. The C cases
cover division/shift, narrow values, source overwrite, calls, loops/edges,
overflow flags, atomics and native asm. All earlier compilable
snapshots are byte-identical: arm64 137/137 with `-g` and asynchronous
unwind tables, wasm32 116/116 and wasm64 119/119; same source hashes.

The base's vpp SHA256 is
`08fa144ebfd7166f9e5b499b6f209413a6ba131cf9374064b5d5c0816744a20c`;
candidate `e8b8d472eb989f2dfe254317b6c81c916f12c1dca2e631584d3e0cd479b7f77b`.
Artifacts and reproducing scripts: `build/linux/opt-copies-baseline/`.
Compiler CPU, three alternating repetitions with four 3000-function TUs,
O1 -S, -j 1, warm=1, caches off (child's user+sys, including warmup):

| Target | Loop corpus ms, base→candidate | Unchanged control corpus ms |
| --- | --- | --- |
| x86-64 | 76.871 → 69.113 | 26.334 → 27.432 |
| arm64 | 90.907 → 90.196 | 32.528 → 33.213 |
| wasm32 | 45.342 → 45.830 | 23.019 → 27.058 |
| wasm64 | 44.886 → 45.779 | 22.625 → 23.036 |

No consistently large x86 regression is visible, but even unchanged wasm code
varies 17.5 % in the control. A shared host and missing hardware counters
mean this does not verify the 1 % gate or a compiler gain. The short
unsigned sum test gives 3.785→3.136 ms CPU (clang O2 0.627; GCC O2
1.776), identical checksum 4096177840. The base's and candidate's assembler
and objects are however byte-identical, so the difference is measurement noise and no
program speedup from the change. Broader copy cleanup, extension facts,
memory optimizations, full O2 coverage and G5/G7 remain.

## Risks
- **The GNU C surface and inline asm** are the big job. Estimated: lexer + pp ~2 kloc,
  parser + sema ~10–15 kloc, emitter ~5 kloc, assembler ~4 kloc, linker ~3 kloc.
- **Correctness with -O0-like code generation.** The kernel assumes inlining and DCE.
  This is solved in sema (steps 2.5–2.6), not through kernel patches.
- **make itself.** Even with v5, make takes ~0.3–0.5 s to read all Makefiles and start
  ~1 000 jobs. The shims' latency and make `-j` become the floor.
- **Measurement noise.** The machine is used for other things. Only A/B measurements run directly
  after each other are compared.

### Optimization rig (2026-11-08): `bench/vbench.py`
- Metric: **instructions** per thread via `thread_selfcounts` (`src/vperf.h`, no root, ~1 600 instr per read), noise 0.03–0.6 % even with load 15; cycles/wall only in `ab` (alternating). The counters include kernel mode (page faults), so `emit` is noisier (~1–2 %).
- `vpp --batch` gained `--warm N` and `--stats F` (per job: ms, instr, cycles, instr in cc_run/emit/obj).
- Corpus `bench/vbench-corpus.txt`: 24 kernel TUs (16 quantiles + emit- and parse-heavy), 9 `quick`.
- Modes: lex, pp, pphc, fe, cc, emit, real, as, ld. `check` hashes -E, token stream, layout, objects (with and without cache), lexer, .S, vmlinux against a base (`mkbase REV`, cache in `work/vbench/golden/`).
- Starting point, full corpus, one thread: lex 783 M, pp 8 074 M, pphc 7 187 M, fe 11 463 M, cc 12 080 M (emit 600 M, obj 14 M), real 11 212 M, as 57 M, ld 763 M instructions. All 840 TUs `-c` without cache: 330 G instr, of which pp ~86 % (with cold token cache).
- **Ten parallel optimization agents (2026-11-08)**, one per lane (lexer, pp macros, pp directives, cc.c declarations / expressions / symbols+tokens, emit, arenas, as/a64/elf, linker), each in its own worktree against base 32bc779, rule: ≥ 1 % fewer instructions and `vbench check` identical. All ten merged; conflicts resolved against the arenas' `vmreg` pools (`src/arena.h`). One merge bug (two lanes moved `dops` into cc with different snapshot handling → crash with the declaration cache) was caught by `check`. Full corpus, base → now (instructions / cycles):

  | mode | base M | now M | instr | cycles |
  |---|---|---|---|---|
  | lex | 783 | 424 | −45.8 % | −26.5 % |
  | pp | 8 066 | 3 245 | −59.8 % | −53.0 % |
  | pphc | 7 125 | 4 706 | −34.0 % | −22.4 % |
  | fe | 11 450 | 5 453 | −52.4 % | −48.3 % |
  | cc | 12 036 | 5 817 | −51.7 % | −45.6 % |
  | emit | 601 | 353 | −41.2 % | −37.0 % |
  | real | 11 232 | 7 354 | −34.5 % | −25.3 % |
  | as | 57.4 | 31.6 | −45.0 % | −45.3 % |
  | ld | 762 | 378 | −50.4 % | −34.4 % |

  Left according to the agents: lazy/skipped parsing of unused `static inline` bodies (1.8 G of fe, changes numbering → needs design), interning misses in the global table (~half of the lex cycles), a cache for inline-asm snippets in emit (~93 M), one-pass tokenization per statement in as.c, open() dominates the linker's kernel time.

  All kernel TUs `-c` in one process (`vcc-cbatch.py -j 4`, all caches on): 124.6 G instructions / 11.9 s user + 0.8 s sys / 3.2 s wall (before: 186 G, 17.4 + 2.8 s at `-j 6`). The suites on the merged tree: parity 873/873 (kernel) and 911/912, sema 878/879 + layout 878/879 (18.8 M checks), cbatch 840/840, vcc-obj 840/840, vas 30/30. **Cold build + boot** (`bench/vccd-kbuild.sh`, `-j 14`, alternating base/new, load ~8): 877 compiled by vcc, 0 by clang; vccd's job time summed 65.6 → 43.6 s (mean 75 → 50 ms, max 484 → 327 ms), but wall time does not move (total 31.1/31.2 → 27.5/42.9 s, compile phase 19.3/23.6 → 17.6/24.6 s; the config phase alone varies 4.6–11.6 s): the compile phase is limited by make/shim/dependency order, not by vccd (cf. Status 2.9). Debian 13 boots to `running`, 0 failed units (`results/boot-20261108-160427.log`). **The next lever for wall time is thus how many jobs make keeps in the air, not the compiler.**

### Why make only keeps ~3 jobs in the air (2026-11-08)
- vccd now logs the start time per job (`@ms`, last argv). Timeline over a cold build (`-j 14`): **jobs start at an even rate, ~100/s, regardless of how long they take**; on average 2.2 in vccd at once, 3.9 s entirely without jobs (of which 1.5 s at the end). It is a start-rate limit, not vccd (64 → now 2 × cores worker threads; no queue in vccd).
- Process samples (`ps` every ~0.1 s) during the compile phase: on average 8.4 `make`, ~6.4 shells (`sh` = bash), ~3 `vcc` shims, ~1 `fixdep`. Kbuild descends into directories with recipes that run `$(MAKE)`, and each such one holds a job slot while the submake lives: with `-j 14` ~8 slots go to the make nesting.
- The build's child processes (without vccd) cost a steady **~25 s user + ~50 s sys** per cold build, more than vccd's own compilation (29–48 s job time). Per C TU the recipe runs `sh -c` + vcc shim + `fixdep` + `rm`; `fixdep` alone ~37 ms wall / ~29 ms CPU per TU (opens all ~930 headers in the dep file to look for `CONFIG_`), an empty process ~8 ms. A no-op rebuild (no CC) takes 8.3–8.9 s / 10 CPU-s.
- `-j 32`/`-j 64` could not be assessed: the machine had load 16 from other sessions and the same `-j 14` took 31–52 s.
- **Next steps:** (1) fixdep in vccd: vccd already has the dep list and all headers lexed in the token cache, so the `CONFIG_` set per file can be computed once and `.cmd` written by vccd; the recipe becomes a single command (no `fixdep`, `rm`, preferably no shell) via a Kbuild patch in `cmd_and_fixdep` when CC is vcc. (2) measure `-j` on a quiet machine. (3) fewer submakes / nesting levels.
- **fixdep in vccd (done, 2026-11-08):** `src/fixdep.c` is a straight port of `scripts/basic/fixdep` (same dep-file parser, same raw-text search for `CONFIG_`, also in comments), but the files are read through the token cache and each file's `CONFIG_` names are computed once per process. vccd runs it after each successful compilation (its own or forwarded) when the request has `VCC_FIXDEP_D/T/O/CMD`, writes `.cmd` and removes the dep file. Kernel patch `patches/0008-kbuild-vcc-fixdep.patch` (branch `vroom-pf`): `cmd_and_vccdep` for `rule_cc_o_c`/`rule_as_o_S` when `KBUILD_VCC_FIXDEP=1` (vccd-kbuild.sh sets it; `VCC_FIXDEP=0` = as before). The recipe now runs shell + shim, not `fixdep` and `rm`.
  - **Verified:** all 1 387 `.cmd` files byte-identical to fixdep's; no `.d` left; no-op rebuild compiles nothing; `touch include/linux/delayacct.h` rebuilds exactly the 13 objects that list it; Debian boots to `running` (`results/boot-20261108-162531.log`).
  - **A/B** (alternating, load 21–47, wall time unusable): the build's child processes 26 + 48 / 27 + 50 s (user + sys) → 20 + 28 / 18 + 26 s, so **~30 CPU-s (40 %) less per cold build**.

### Profile of the cold build after fixdep in vccd (2026-11-08)
- Rig: `bench/tsh.c` is a tracing `SHELL` (runs `/bin/sh`, logs start/end in `CLOCK_MONOTONIC` like vccd's `@ms` and the shell's and children's rusage); the binary must be named `sh` so that make keeps its fast path. `TSH_LOG=OUT.tsh bench/vccd-kbuild.sh OUT -j 14 SHELL=…/trace/sh` (the script now passes on extra make arguments), then `bench/kprof.py OUT`. The tracing costs ~1–2 s. Load 4–28 from other sessions during the measurements.
- **Cold build now (this branch, `-j 14`):** 15.2/14.5/14.0 s (main without the branch's work: 24.0 s at the same load). Breakdown: **config 3.5 s** (kbuild.py before the main make: `allnoconfig` 2.2 s, which builds fixdep/conf/bison/flex from scratch with host compilation via vroomd; `olddefconfig` 0.7 s; `merge_config` 0.05 s) → **1.5–1.9 s to first CC** (makefile parsing, `conf --syncconfig` 0.9 s, generated headers) → **compile phase ~8.5 s** → **link 0.6–0.9 s**.
- **The compile phase is CPU-bound, not job-slot-bound:** `-j 28` gave 17.0/17.3 s against `-j 14` 15.2/17.5 s. During the phase ~12 leaf recipes and ~20–25 submake recipes run at once, but only 2–7 compilations in vccd. The build's child processes (everything but vccd) cost 17 s user + 23 s sys = **40 CPU-s, more than vccd's 32 s job time**.
- Per CC recipe: **15 ms (median) before the job reaches vccd** (bash + shim + connection), 28 ms in vccd (p90 74 ms), ~2 ms after (fixdep is gone). Summed 17 s before against 32 s in vccd.
- **`llvm-ar` is the largest single process cost:** 416 `AR built-in.a`, 29 s of job-slot time, median 43 ms, p90 149 ms (up to 290 ms), 44 ms CPU each during the build. One call costs **167 M instructions** (dyld binds `libLLVM.dylib`; 1 800 page faults), against 12 M for the vcc shim, 22 M for `/bin/sh` (bash) and 8 M for `/bin/dash`. 416 × 167 M ≈ 70 G instructions, more than half of the kernel compilation's 124.6 G.
- **Submakes:** 432 recursive makes (one per directory). A no-op rebuild takes 2.7 s / 8 CPU-s almost only in them (plus 400 `depcheck`).
- **`SHELL=/bin/dash CONFIG_SHELL=/bin/dash`** (alternating A/B): 14.5/14.0 → **13.0/13.4 s**, child CPU 40 → 36 s. `/bin/sh` on macOS is bash.
- **Levers in order:** (1) our own `ar` for thin archives (`cDPrST`, flatten nested thin archives) as a vcc shim/vccd request instead of `llvm-ar`: ~70 G instr and ~29 s of job-slot time; (2) dash as `SHELL`/`CONFIG_SHELL` in the build: ~1 s, no code changes; (3) the config phase 3.5 s: the host tools (conf, fixdep, …) are built by clang via vroomd from scratch, and `allnoconfig` + `olddefconfig` are two extra top-level makes (either `--dotconfig` with a ready `.config`, or host tools from vcc/cache); (4) fewer submakes; (5) the shell + the shim per CC (15 ms): the recipe without a shell when the command is a single vcc call.
- **Own `ar` in vccd + dash (done, 2026-11-08):** `src/ar.c`: thin archives with r/m/t and the modifiers c D P s S T u i b a plus `--thin`, everything Kbuild does with `$(AR)` (`cDPrST` per directory, `cDPrsT` for lib.a with a symbol table, `mPi` + `t` for the built-in fixup.a, `cDPrS --thin`). Byte-identical to llvm-ar, including its rules: nested thin archives are flattened, `r` replaces an existing member in place, `m` moves in archive order before/after POS (POS is compared literally with stored names), symbol table only if some member is ELF (empty: 8 bytes), paths relative lexically. The shim under the name `var` (also `ar`, `llvm-ar`) → vccd runs it in the worker thread; **no llvm-ar in the build, no fallback**. `bench/kbuild.py --ar/--shell`; `bench/vccd-kbuild.sh` uses `var` and `/bin/dash` as `SHELL`/`CONFIG_SHELL` for the whole run (`VCC_AR=0`, `KB_SHELL=` = as before).
  - **Verified:** `bench/var-cmp.py` (llvm-ar only as reference): all 416 saved archive commands in an llvm-ar tree give byte-identical archives, `t` identical for 8 path forms, 40/40 edge cases (duplicates, nested, missing file, r on an existing archive, mPi/mPa/mPb, ELF without symbols, non-ELF, archive in a subdirectory). Cold build: all 417 `.a`, `vmlinux` and `Image` byte-identical to the llvm-ar + bash build; `.cmd` differs only in the AR path and `CONFIG_SHELL`; Debian boots to `running`, 0 failed units (`results/boot-20261108-171002.log`).
  - **A/B** (alternating ×3, load 9 → 22 from other sessions, wall time unusable: 17.4/24.2/25.2 → 18.0/21.9/21.3 s): the build's child processes **18 + 25 → 11 + 20–21 CPU-s (user + sys), ~12 CPU-s (27 %) less**. ar in vccd: 422 jobs, 3.6 ms on average (before ~44 ms CPU per llvm-ar process).
- **Merge with main (diagnostics, _Complex, long double, … up to 8094aeb, then sanitizers/fuzz up to f323896), 2026-11-08:** main@8094aeb did not boot (no console output at all). Bisected to 26c2d50 (floating-point `va_arg`): every variadic function saved v0–v7 in the prologue even with `-mgeneral-regs-only`, so the kernel's first `printk` touched FP registers before FP was enabled. Now as clang: with `-mgeneral-regs-only` no v save area, `__vr_offs` 0 and `__vr_top` = `__gr_top` (`cc_opts.general_regs_only`). In addition: the declaration cache's snapshots carried other TUs' `c->cases`/`c->span` along (11 false "duplicate case value" in cbatch); `span` is now an array of its own in the image (with place encoding), `cases` is kept per TU.
  - **Verified:** parity 911/912, sema run 878/879, warn 879/879, layout 878/879, ctests 1708/1708, diag 63/63, cbatch 840/840 (same stderr with and without declaration cache), vcc-obj 840/840, vas 30/30, var-cmp 416/416 + 8/8 + 40/40. Cold build 13.9 s (load ~8), 877 by vcc, 0 to clang; Debian boots to `running` (`results/boot-20261108-174855.log`). After the second merge (f323896): same numbers, fuzz-regress 4/4, `pp_open` now writes "1 error generated." even when the main file has a fatal error (diag 63/63); cold build 15.3 s (load ~11), boots (`results/boot-20261108-175726.log`).
  - **Why it did not boot, confirmed:** the kernel without the fix in QEMU/TCG with `-d int`: ESR class 0x07 (FP/SIMD access trapped) at `_printk+0x2c` (`fmov x16, d0` right after `str x0..x7`), the first `printk` in `start_kernel` before `setup_arch`, so before earlycon; the trap re-enters `printk` and repeats (~1.5 M times in 25 s), no output. Without the fix 83 variadic functions in `vmlinux` save v0–v7, with it 0. vcc-obj did not see it (compares symbols and relocations), ctests are user programs.
  - **Guard:** `bench/vcc-fpcheck.py` compiles all 840 kernel TUs with `-mgeneral-regs-only` (`vpp --batch -S`) and fails generated FP/SIMD instructions (b/h/s/d/q/v/z registers, FP mnemonics, `.inst` words with bits 27:26 = 11) outside inline asm, which `-S` now surrounds with clang's `//APP`/`//NO_APP`. 840/840 now; with the fix switched off 801/840 (`panic`, `__warn_printk`, `sysfs_emit`, …). ~10 s.

### Profile of the cold build on macOS, 2026-11-08 (after the Linux merge)
- Rig: `TSH_LOG=OUT.tsh bench/vccd-kbuild.sh OUT -j 14 SHELL=…/trace/sh` + `bench/kprof.py OUT`
  (now also job slots per second and the recipes before the first CC); `sample <vccd> 25` +
  `bench/vccd-sample.py` (new) for vccd's CPU per step. At exit vccd writes
  the main cache's and the declaration cache's counters and its own rusage.
- **15.0 s** (load ~5): config 2.9 s (`--config`: allnoconfig + olddefconfig build conf/fixdep
  from scratch via vroomd) → 1.9 s to first CC (`conf --syncconfig` 0.39 s, the dtc host tools
  via vroomd/clang 100–270 ms each) → compile phase ~7 s → link 0.7 s.
- **CPU:** vccd 16.9 s user + 1.7 s sys. The rest of the build (make, shell, shims) 14 s user +
  **27 s sys**. Leaf recipes: CC 2.7 + 4.5 s, AR 2.3 + 9.5 s; the rest (~15 s) is the 432 make processes.
- **The compile phase has two halves:** first 7–9 TUs at once in vccd, then the
  archives decide: up to 6 AR recipes in the air while the compilations sink to 2–3. An AR recipe
  takes ~55 ms wall (shell + `rm` + `var` shim + printf), but only 2 ms in vccd. `built-in.a`
  chains upward in the directory tree, so it is the critical path. Per CC: 13 ms (median) before vccd
  (shell + shim), 34 ms in vccd (p90 94).
- **vccd's CPU** (59 k samples): preprocessing as the parser pulls it 43 %, parse + sema 36 %, fixdep 6 %,
  emit 5.5 %, I/O thread 2 %, writing 2 %. Hottest: `rec_tok` 11 % (recording into
  the main cache, also of replayed child headers), `pp_next` 9 %, `decl_specs`/`declarator` 4 % each.
  Main cache: 40 790 hits, 12 095 misses (11 069 recorded, 43.7 MB), 548 MB replayed;
  declaration cache 845 restores.
- **Levers:** (1) shell and processes per recipe: CC = shell + shim, AR = shell + `rm` + shim
  (next step); (2) the config phase (ready `.config`); (3) the recording in the main cache.
- **Recipes in vccd: vsh + rsh (done, 2026-11-08):** make's `SHELL` is `build/src/vsh/sh`
  (shim.c with `-DVSH`; named `sh` so that make keeps the fast path). It sends `sh -c SCRIPT`
  to vccd, which interprets the script with `src/rsh.c`: Kbuild's subset of sh (`set -e`, `:`,
  `trap` (ignored), `echo`, `printf` with `%s`, `rm -f`, `> FILE`, `NAME=V tool …`,
  `printf … | xargs tool …`, `scripts/basic/fixdep D T CMD > FILE`, quotes and `\X`).
  Tools = vcc/vas/vld/var next to vccd; they run as ordinary requests in the same thread
  (fixdep afterwards as in 0008). The whole script is parsed before anything runs; everything else (`$`, `&&`,
  subshell, glob, `>>`, unknown commands …) is declined and vsh runs `$VSH_REAL` (dash) with the same
  argv. No Kbuild patch. `bench/vccd-kbuild.sh` uses vsh by default (`VSH=0` = dash).
  - **Verified:** `bench/vsh-test.py` 32/32 against dash (status, stdout, stderr, files; cases that
    must run in vccd get `VSH_REAL=/bin/false`, declined ones are counted), also with ASan and on Linux
    (29/29 without built fixdep). Cold build: all 3 477 files in the build tree (`.cmd`, `.o`, `.a`,
    `vmlinux`, `Image`) byte-identical to the dash build; no-op rebuild compiles nothing;
    Debian boots to `running` (`results/boot-20261108-192143.log`). One crash on the way:
    `vfixdep` before the first compilation (after a forwarded host link) interned without
    `vintern_init`.
  - **Measured (macOS, `-j 14`, alternating, load 5–11):** 1 359 recipes run in vccd (all 854 CC,
    416 AR, HOSTCC, the fixdep recipes), 1 378 declined (mostly `$(shell …)` and config, 0.1 ms
    each). Total 13.6/13.0 s (dash) → **11.6/11.9 s**; the build's child processes 10 + 18 →
    **8 + 10 CPU-s** (user + sys). kprof: CC before vccd median 13.2 → 5.7 ms (with tsh), AR recipe
    55 → 8 ms (sum 22.9 → 3.3 s), the recipes' CPU CC 7.2 → 1.9 s, AR 11.8 → 0.9 s. Linux (`-j 8`):
    fork/exec was already cheap, 9.8/8.9 → 8.8/13.0 s, noise.
  - **Left:** the 432 submakes (≈ 100 CPU-s incl. children in the trace) and `$(shell)` (vsh
    declines, an extra round trip per call).
- **New profile with vsh (2026-11-08) and two low-hanging fruits:**
  - The leaf recipes are now cheap (CC + AR ~3 CPU-s). Left in the trace: 422 `$(shell mkdir -p
    $(obj))`, one per submake during parsing (vsh + dash + mkdir, median 14.5 ms, 6.2 s wall
    summed). **rsh now runs `mkdir -p`** (mkdirat, parents from the top): 1 786 recipes in vccd,
    951 declined.
  - vccd: `rec_tok` 13 % of the samples. During recording every token was copied from a replayed
    child entry into the recording, although publishing skips the child segments. **Now
    only the positions are reserved** (`ctx.norec`, `ntk += total`). cbatch (all kernel TUs `-c` in one
    process): 150.7 → **135.4 G instructions** (−10 %), 48.0 → 41.1 G cycles, user 15.0 → 12.8 s.
    Cold build: vccd user 16.7 → 14.6 s.
  - **Verified:** parity 911/912, batch --cmp 873/873, cbatch 840/840 (also ASan), fuzz-regress,
    vsh-test 35/35 (also ASan); the build tree (3 478 files) byte-identical to the dash build; Debian
    boots (`results/boot-20261108-193415.log`). Wall time 12.1 s at load ~10 (noise).
  - **Next candidates:** the config phase has two serial rounds with 125 Kconfig `$(success,…)`
    each (521 + 308 ms), `{ …; } >/dev/null 2>&1 && echo y || echo n` with `${o#-Wno-}` etc. —
    either more sh in rsh or a Kconfig patch that asks vccd directly; 60 host compilations
    via vroomd before the first CC; the 432 submakes.

### Profile of vcc/vccd's own work (2026-11-08): what should be rebuilt
- Rig: `bench/vccd-kbuild.sh` (vroom.config, `-j 14`) with `sample <vccd> 25` +
  `bench/vccd-sample.py`; the same TUs without a build system in `vpp --batch` (like
  `vcc-cbatch.py`, `--stats`, `-j 1` under `sample`, `--warm 1`, `VCC_NODC`/`VPP_NOHC`);
  `bench/vcc-bodies.py` (new) for which tokens the parser gets. Load 5–6.
- **The build:** 13.9 s, 877 TUs by vcc. vccd 15.6 s user + 2.4 s sys; 877 `cc-own` sum
  to 46.4 s wall (median 39 ms) against ~13.6 s CPU in batch (median 14 ms): the jobs wait for CPU
  (9–11 in the air, make and other sessions share the cores), not for locks (84 % of the blocked
  samples are idle workers; no mutex waits in the compilation). vccd uses ~2.1 cores on
  average during the compile phase, so it is still make that sets the wall time there.
- **The compiler alone** (840 kernel TUs, one process, cold): 131 G instructions, IPC 3.4,
  12.2 s on one thread, 2.3 s on 6. cc_run (preprocessing + parse + sema) **98 %**, emit + as
  3 %, objects 0.8 %. The samples without waiting: parse + sema 50 %, preprocessing as the parser pulls it
  42 %, emit 5 %, writing 2 %. High IPC: micro-optimization is largely done; what
  remains is to do less work.
- **The caches' value:** without both 189 G; the main cache 189 → 151 G (−20 %); the declaration cache
  151 → 135 G (−10 %; only TUs with the same *prefix* of top-level headers, 1.5 regions
  per restore on average, ~19 % of tokens). A fully warm process (`--warm 1`, = a vccd that survives
  between builds) 133.6 → 106.7 G (−20 %): the rest is per-TU work that no cache removes.
- **What the parser gets** (`vcc-bodies.py`, 24 corpus TUs, 20.8 M tokens): 95 % comes from
  headers; **51.6 % are bodies of functions in headers and 49.2 % bodies the TU never reaches**
  (transitively from the main file and initializers); the main file's own bodies 4.1 %. Typical TU:
  ~8 000 inline functions in headers, ~7 800 unused. In time: `compound_stmt` including the
  preprocessing it pulls is **55 % of the compilation's CPU** (27 % pp, 28 % parse).
- **Rebuilds in order:**
  1. **Lazy parsing of function bodies in headers** (~−45–50 % of the compilation's CPU if
     the skip is free, ~−25 % if only the parsing is saved). At `{` after a
     file-scope function declarator: save the token range and skip to the matching `}`;
     parse when the name is first referenced (call, address, inlining ≤ 40 nodes), otherwise never
     (emit is already lazy). The expensive part is that tokens are still pulled through `pp_next`; therefore
     the main cache's entries should carry the body ranges (counted at recording: depth 0, `)` `{`)
     so that the replay skips a body in O(1) and the parser later reads it directly from the
     entry's token array (zero copy). Name lookup in a late body: symbols carry a
     declaration order and the lookup ignores later declarations (exact C semantics
     also for late tags/redeclarations). Cost: diagnostics in unused bodies disappear
     (clang gives them) → `VCC_EAGER=1` for the sema/diag suites; numbering (strings, static
     locals) follows use order, which clang's CodeGen also does (deferred decls).
  2. **Declaration cache per header instead of per prefix** (module-like): when a header entry
     is replayed with the same macro inputs and the same *semantic* inputs (the names/tags its
     declarations looked up and what they bound to), apply the entry's declaration delta
     (types, symbols, tags) instead of parsing. After (1) what remains of
     headers is mostly declarations (~40 % of today's time); the prefix tree reaches only ~19 % of tokens.
  3. **fixdep in vccd** 6 % of vccd's CPU in the build (`in_hashtable`, mutex waits in `vfixdep`):
     the `CONFIG_` set per file as interned ids, union per TU with a generation-stamped
     array, no global lock. **Lock gone (2026-11-10, from the branch):** the names per file
     hang in `vlex_file.fixdep` (CAS), no mutex table.
  - **Diagnostic intervals only when something is reported (2026-11-10, from the branch):** `pp_tok_end`
    was computed at every assignment (`need_lvalue`), every `/ % << >>` with a constant and every
    `sizeof (T)`; now only on the error path. The branch measured cbatch 135.4 → 128.7 G instructions
    (−5 %, before the lazy bodies). `vpp --batch` now writes the number of tokens to the parser and
    how many the declaration cache skipped.
  4. Small things: `VCC_LOG`'s `fprintf` takes `flockfile` locks (profiling only); `open()` when
     writing 1 %.
- **Lazy bodies, step 1: at the parser level (done, 2026-11-08).** `frontend/c/lazy.c`: a `static`
  function defined in a header (not the main file; without used/constructor/destructor/alias,
  not overloadable, no `#pragma pack` active) gets its body saved as tokens (`lztok`,
  16 bytes; identifiers as interned id, the others' spelling in `lzs`), `SF_DEFINED | SF_LAZY`,
  and the token stream continues after `}`. After the TU the bodies that something names are parsed
  (`SF_REF`: every `N_VAR`, also in sizeof/typeof, block-scope `extern` marks the global,
  `cleanup()`, alias targets), until nothing new is named. Name lookup in a late body skips
  file-scope symbols created after the definition (`lz_wm`, along `shadow`), i.e. exactly
  what was visible there. `#pragma` in the body is saved as `P_DIRECTIVE` and executed when the body
  is parsed (`fetch_directive` reads on from the body). The declaration cache's images have three new
  arrays (places are encoded). Emit aborts with an internal error if a never-parsed body is referenced.
  `VCC_EAGER=1` = as before.
  - **Measured** (840 kernel TUs, one process, `-j 6`, same binary with/without `VCC_EAGER`):
    137.6 → **101.7 G instructions (−26 %)**, 43.8 → 35.1 G cycles, user 14.1 → 10.9 s.
    4.39 M bodies saved (264.6 M tokens), **85.6 k parsed (6.5 M tokens, 2 %)**. The cycles
    drop less than the instructions: the copying of tokens is memory-bound, and they are
    still pulled through `pp_next` (step 2). Cold build (`-j 14`, alternating ×3, load 7–9):
    vccd user 14.5–15.1 → **12.1–12.3 s**, summed job time 26–28 → 22–24 s; wall time
    10.8–11.0 → 10.4–10.9 s (make-bound, as expected).
  - **Verified:** cbatch 840/840 (also ASan+UBSan), vcc-obj 840/840 with the same 31 undefined
    symbols as without lazy bodies, sema run 878/879, layout 878/879 (18.8 M checks),
    fpcheck 840/840, diag 63/63, ctests 1708/1708, parity 911/912. The objects against `VCC_EAGER=1`:
    machine code identical in all 839; 284 differ in byte form, only placement: strings in
    `.rodata.str1.1`, the order of static local objects (`__msg`) in `.rodata`/`.data`,
    the suffix of static locals' names (sym index) — they are created when the body is parsed. Debian
    boots to `running`, 0 failed units (`results/boot-20261108-214004.log`).
- **Lazy bodies, step 2: in the main cache (done, 2026-11-08).** `publish_tok` saves per entry
  the outermost `{`–`}` pairs in the entry's own tokens that have no child spliced in (`hent.br`).
  When the parser's `{` for a lazy body was just replayed from an entry and the pair is there
  (`pp_lazy_skip`; not during recording except in a `norec` replay, no `pend`),
  the replay is moved past `}`: the positions are counted (`ntok_out`, the declaration cache),
  the `__COUNTER__` entries are skipped. The body is then `(entry, open, close, base, cbase, ci)`
  (`pp_lazy` in `lzfn`), and the late parse reads the entry's tokens directly (`pp_lazy_tok`,
  the same `__COUNTER__` rewrite as `replay_tok`, now `ctr_spell`). Misses in the main cache
  copy as in step 1. `prevloc`/`prevflags` become `}`'s as before.
  - **Measured** (same run as above, 840 TUs, `-j 6`): step 1 101.7 → **76.0 G
    instructions** (from 137.6 without lazy bodies: **−45 %**), 35.1 → 27.4 G cycles (−37 %),
    user 10.9 → 8.3 s, wall 2.1 → 1.6 s. 4.21 M bodies saved, 3.81 M from the main cache,
    the same 85 647 parsed. The declaration cache now takes 121 images (before 64) within the same cap.
    Cold build (`-j 14`, alternating against `VCC_EAGER=1` ×3, load 5–13): vccd user
    14.8–15.1 → **9.4–9.5 s (−37 %)**, summed job time 30–42 → 19–24 s; wall time noise.
  - **Verified:** all 839 objects byte-identical to step 1; parity 911/912, batch --cmp
    873/873, sema run and layout 878/879, fpcheck 840/840, diag 63/63, vcc-obj 840/840 (same
    31 undefined), cbatch ASan+UBSan 840/840, fuzz-regress, vccd-cc 174/175 (as before;
    note: with a long `TMPDIR` the socket path becomes too long and vccd-cc hangs in `forward`).
    Debian boots to `running`, 0 failed units (`results/boot-20261108-220251.log`).
- **Incremental:** the make level already works (vccd's `.cmd` = fixdep's, `touch` on a header
  rebuilds exactly the dependent objects; no-op 2.7 s, almost only submakes). A vccd that lives
  between builds gives −20 % instructions. What gives a lot is a **content-addressed
  object cache in vccd** (direct mode like ccache: key = vcc build id + argv + hash of
  the main file and each file in the previous run's dep list; file hash once per process in
  the token cache; hit = write `.o` + `.cmd`), mmap'ed on disk. A rebuild in an empty output
  directory then gets close to zero compile time; the first cold build is not helped.

### Error messages (clang format)

Status: `bench/vcc-diag.py` compares vcc's stderr with clang's (-fsyntax-only) on
`src/test/diag/*.c`: 63/63 identical (from 4/48).  `bench/vcc-sema.py
warn` runs vcc and clang with the kernel's flags on all 879 TUs and requires that
every diagnostic vcc writes is also written by clang: 879/879 (no false
warnings).

The engine in pp.c writes clang's format (source line, caret, ranges `~`, fix-it line
with clang's padding, "In file included from", colors, "N errors generated.",
the path as spelled, "./a.h" for includes from a file without a directory, also
in `__FILE__` as in clang).  cc.c: ranges per node (`c->span`),
`report()`/`note()`/`warnx()`, type names, spelling suggestions with recovery,
NF_ERR against follow-on errors, "expected ';' after …" with fix-it, int-conversion and
incompatible-pointer-types per context with a parameter note, number of arguments
("expected single argument 'a'", the parameter list's range in "declared
here"), duplicate case values/members, tentative incomplete types,
static_assert with "expression evaluates to", `.`/`->`, unmatched parentheses,
"declared const here" with the declaration's range, -ferror-limit (19 errors +
fatal without position).  Warning framework: -W/-Wno-/-Werror(=)/-Wno-error=/-w in
order, groups (all, most, everything), silent in system headers and for
operators from macros (PF_MACRO): -Wparentheses, -Wunused-comparison,
-Wswitch, -Wmacro-redefined.  pp: #error/#warning, unterminated conditionals,
missing include, macro arguments with "macro 'X' defined here".

"expanded from macro": caret and range over the whole call, the note at the
body's token (found at the diagnostic via the `#define` line, so tokens carry no
extra position); no spelling suggestions for names from macros (as clang).
Reachability warnings: -Wdivision-by-zero, -Wshift-count-overflow/-negative,
-Warray-bounds ("array 'a' declared here"), silent in code that is not reached
(if/?:/&&/|| on constants).  So far only for undeclared identifiers in
macros.

Left: the note for more diagnostics than undeclared names, nested macros (one
level today), more -Wall warnings.

### Integration on current main (PR series, 2026-11-11)

X86's newly added debug lines are now remapped through both unreachability and
fallthrough compaction, and the end line is also remapped through twoaddr.
`bench/vcc-opt-debug.py` checks source lines at live operations,
.text with/without -g and dwarfdump --verify at O0–O3; part of CI.

### OPT3 on current main (PR series, 2026-11-11)

The register homes' ABI saving now uses the x86 writer's existing csslot
for prologue/epilogue and correct CFI, also with realignment. The separate
lsave/restore hook is removed. Asm also takes private snapshots of
the current register homes when fixed operand/clobber registers overlap them;
temporary ownership is restored on each edge. All asm outputs are kept addressable
by the prepass's lnoreg rule, so their results are written before the snapshot restore.
Fixed RBX outputs and asm-goto outputs are tested against a simultaneously live home.

Rebased unity and file by file build. Unity: 116 focused
rule/level/target combinations with running, including narrow parameters and
setjmp/alignment. The relevant eight x86 local/nonlocal cases also file by file.
X86 cross-ABI 22/22. `bench/vcc-x86-unwind-locals.py` uses libgcc's real
unwinder to recover the caller's RBX/R13–R15 through vcc functions with a
plain and a 64-byte-aligned frame at O1/O2, both build modes. The clang reference
passes the test; an older audit binary is rejected. The script requires native x86 Linux,
CI's arm64 job still runs the libc-free x86 probes under QEMU.
`bench/vcc-opt-debug.py` and the ASan/UBSan effect contracts green.

These results do not replace a new performance gate on current main.
The earlier measurements above keep their documented bases/hashes.
G5/G7 and full O2 coverage are still open.

### OPT4 on current main (local continuation, 2026-11-11)

The copy pass has been moved to the PR series' new base in a separate local branch
`opt/native-copies`; not yet part of the five open PRs. X86's
compaction now remaps each debug index and the end line before twoaddr.
The debug regression has an extra function-local marker after removable
register copies. Unity and file by file build; 16 new rule/level/target cases
with running pass in unity, four x86 cases also file by file. Debug lines,
.text equality and DWARF structure pass O0–O3 in both build modes; sanitized
record contracts and real native unwind through plain/aligned frames
pass. Older measurements above are still tied to their older base;
a new performance gate and PR/CI for this continuation remain.

The shared host's /tmp ran out of inodes. 756 existing snapshot files were
archived and verified byte for byte before local cleanup in
`/source/vroom/work/opt-audit-artifacts/copy-snapshots.tar`; JSON/hashes and
measurement data remain in the original opt-copies-baseline directory.
### Delivery integration (2026-11-11)

PR33–36 were merged into their stack bases after the preceding base was already
merged to main. Their optimizations were therefore not in main. The branch
`opt/optimizer-integration` collects the already reviewed changes with
main b0b483d and keeps all CI checks, including the C2y cache.
Unity, 116 focused run combinations on four target configurations,
debug-line/DWARF/text check and real x86 unwind O1/O2 are green after
the integration. The prepass's thread publication and VLA facts are reviewed against
the register selection. New CI is required on the integrated commit.

### OPT4 cost review after integration (2026-11-11)

The eager x86 adapter is rejected for publication: paired against d1ee6f6 it gave
compiler CPU loop 68.524→75.594 ms and control 26.064→27.775 ms,
with a consistently higher candidate sample. Artifacts:
`/source/vroom/work/opt-pr-tmp/copies-measure/integrated-pair/`.

X86 now decodes effects on demand via the shared copy core. No
own allocation, full effect/liveness table or backward walk. Without a
live table, an explicit full overwrite of the temporary is required; a change to the
source or an opaque boundary aborts the whole transaction. This is more
conservative when a value died before a later source change. Arm64 uses
its existing tables without new analysis. The copy call is also inlined into
the existing twoaddr walk; separate scan/compaction passes
are removed and the same debug remapping handles removed copies.
Return copies before a jump/label are rejected without effect decoding.

The new pass keeps cp_stores 26→23 instructions and 12→9 register
copies. Sanitized contracts, x86/arm64's focused O0–O3 runs,
debug lines/DWARF/text and real unwind O1/O2 are green. New measurement against
14379fe: before the fast path x86 loop 64.638→66.536 and control
25.153→26.793 ms. With the fast path, at a different load level, loop
85.539→94.029 and control 30.644→29.524 ms; the previous candidate in the same
measurement gave 103.384 and 31.356 ms respectively. Absolute numbers between rounds are
not comparable. The loop overhead is still an open cost question,
not a proven approval of the 1 % gate. `ondemand-pair/`,
`fused-pair/` and `fastpath-pair/` contain raw samples, hashed binaries and
candidate patches under the same copies-measure directory. The copy step remains
separate from integration PR38; no new PR for it yet.

### OPT5 concrete implementation order

1. Move arm64's existing cell/overlap/escape analysis and slots pass
   to `codegen/rec/slots.c`, reuse the existing rr/ww/live/block tables.
2. Describe accesses with a stable allocation ID (also a second ID
   at one-past), offset/width/value register and an explicit observable marker.
   X86's current LD/ST lack volatile/atomic facts: these must come
   along from the typed walker calls before any DSE can be enabled.
3. X86 starts with exact GPR cells 1/2/4/8 bytes; a load is replaced with
   MOV/EXT according to the right signedness. Untagged/overlapping/observable
   accesses block optimization. Test especially unread volatile stores,
   scope reuse, address escape, one-past, unions, calls/asm/atomics.
4. Wasm shares the cell analysis but needs stable local value IDs
   and structured memory-operation records or scalar replacement before
   text emission. No rewriting of finished assembler text; address
   producers, narrow extensions and stack balance must be preserved.

### Verified delivery (2026-11-11)

Integration PR38 is merged to main with head 14379fe. The final CI run
43017255611 passed arm64, wasm, ASan, TSan and gate. The earlier failure
with missing X_FSQRT effect metadata is fixed, with targeted contracts for
scalar FP results and preserved upper SIMD lanes. The DNS failure from the first
ASan installation did not recur. OPT4's new copy work remains in a
separate local branch and is not yet published because of the cost question.

### OPT4: directly known zero extension and instruction profile (2026-11-11)

`arch/x64/codegen/extensions.c` now proves from only the preceding record that a
GPR result is already zero-extended from 32 bits. `x64_ext` then omits
its redundant unsigned 32→64 normalization at O1+, !noopt. No new
walks, tables or allocations. The rule applies to explicit full
32-bit writes and unsigned loads/extensions, not unknown
records, labels, partial/signed writes, shifts or BSF/BSR.
Signed normalization is kept. Arm64's existing extension pass and wasm's
typed operand stack are not changed by this instruction property.

The new known_extensions.c runs O0–O3 on x86, arm64, wasm32 and wasm64.
X86's function-bounded code requirement rejects the older binary. High 64 bits,
negative values, narrow types, wraparound, shift by 0 and volatile reads
are checked; register/width contracts pass ASan/UBSan. The debug probe is green.

Valgrind 3.26 was installed only in a local artifact directory, not in
the system. Native-march builds use AVX512 which the tool does not support;
both profiling binaries were therefore built identically with -O3
-march=x86-64-v3. Base is 14379fe. 500-function TUs, target x86-64 O1 -S,
whole process including startup, Callgrind Ir:

| Corpus | Base | Copy pass | Copies + direct extension rule |
| --- | --- | --- | --- |
| Loop | 35 695 355 | 36 202 950 (+1.42%) | 35 775 862 (+0.23%) |
| Control | 16 278 936 | 16 328 031 (+0.30%) | 16 158 711 (−0.74%) |

This makes the cost reproducible for these v3 probes; it is not a new
vbench/native CPU/whole-corpus gate. The native CPU samples above have large host-load
variation. Raw profiles, the versioned local toolchain package file and binaries are in
`/source/vroom/work/opt-profile/`. Final verification and CI on current
main are still required before delivery of OPT4.

### OPT4 verification on new main (2026-11-11)

Main 9d022c3 is integrated without removing its bitcast/packed-bool support.
Unity and file by file build. 148 focused rule/level/target combinations
with running are green (40 x86, 36 each for arm64/wasm32/wasm64); eight
x86 copy/extension cases and the debug check repeated file by file.
Both host contracts pass ASan/UBSan. X86's real unwind and
function-bounded debug/text/DWARF checks are green. New CI follows the
PR; the CPU measurements and the instruction profile above have their explicit
older bases and are not a general approval of G5.

### OPT6 status: bitmask for switch membership (2026-11-11)

A limited shared analysis recognizes at least four stacked case labels
with the same entry and at most 64 possible values. Arm64, x86, wasm32 and
wasm64 use a bitmask with an unsigned range check before the shift at
O1-O3; the default, bodies and fallthrough keep their original labels.
Dense masks, distinct entries, hidden cases, `_BitInt` and larger spans use
the existing dispatch. O0 and `VCC_NOOPT` are unaffected.

`vcc-switch-bitsets.py --run` passes O0-O3 and `VCC_NOOPT` on all four
targets, including signed/unsigned limits, ranges, side effects and the
fallback. Unity and file-by-file builds work; the file-by-file build also
runs the x86 and wasm64 tests. The vcc suite passes 246/246 with one
existing xfail. On the exact parent 032e9dc compared with the change, the
x86 O2 function `bs64` goes from 131 to 85 bytes (without alignment), a
targeted code-size probe. Twelve alternating runs of a corpus of 500
matching switch functions gave a median of 4.52 → 4.34 ms for the whole
compiler (ranges 4.27-5.06 and 4.13-4.76 ms respectively); these short wall
times are not a general performance gate. Hardware counters were
unavailable on the host. Value tables and bitset clusters with several
destinations remain.

### OPT6 status: balanced switch dispatch (2026-11-11)

Shared policy, 32/64-bit normalization and sorting are in
`codegen/opt/switch.c`; arm64, the shared x86 walker and wasm use it for real
balanced comparison trees at O1-O3 and at least eight case ranges. Small
switches keep the cheap chain; wasm keeps its dense `br_table`. O0,
`VCC_NOOPT` and the 128-bit fallback are unchanged. The selector is
evaluated once. Case bodies, label identity, fallthrough and the goto model
are not moved. If normalization flips/overlaps ranges or allocation fails,
the existing chain is used. No IR, SSA or extra function pass is built.

`switch_dispatch.c` and `vcc-opt-rules.py --rule switches --run` pass
O0-O3 for x86, arm64, wasm32 and wasm64: unsorted sparse cases, GNU ranges,
signed/unsigned 32/64-bit limits, ranges across zero and almost the whole
unsigned64 domain, default without/with cases, fallthrough, nested case in
if(0), goto past the selector and wide fallback. An actually printed CFG
with 31 sparse cases has at most 9 conditional decisions on x86 and 10 on
arm64 (previously 31); wasm has four balanced split levels and keeps the
dense table. The old base's chain fails the code requirement. Sanitized
contracts check normalization, label identity, overlap, density overflow and
the table limit. Unity and file-by-file builds work; the file-by-file build
also runs the x86/wasm64 cases O0-O3. The O0 and `VCC_NOOPT` assembler is
byte-identical to the base for all four targets.

Cost against exact fded15a, identical clang23 builds with
`-O3 -march=x86-64-v3`, Callgrind 3.26 for the whole compiler process
`vpp --target=... -O2 -S`. Three bounded corpora of 500 functions:

| Target | Straight integer code, Ir | Summation loop, Ir | Small three-case switches, Ir |
| --- | ---: | ---: | ---: |
| x86 | −0.0123 % | −0.0069 % | +0.0351 % |
| arm64 | −0.0242 % | −0.0104 % | +0.0356 % |
| wasm32 | +0.0095 % | +0.0258 % | +0.1795 % |
| wasm64 | +0.0097 % | +0.0259 % | +0.1794 % |

All twelve assembler pairs are identical. Raw instruction counts, binary
hashes and commands are in `work/opt-profile/switch-profile.json` and the
`.cg`/`.log` files, with the reproducible meter `switch-measure.py`. The
x86 O2 binary of the portable switch regression executes 1 183 812 →
610 952 instructions (−48.39 %) and 396 541 → 109 244 conditional branches
in Callgrind; this is a targeted code-quality probe, not a general
runtime/native cost gate. `switch-runtime-profile.json` holds the
hash/commands for exactly this probe. Value tables, extended inlining and
unused local initializers remain in OPT6; the other O2 steps and the broad
G5/G7 gates are still open.

OPT6 integration: 19c6a63 is the switch implementation above; after it
PR47's main update b63c269 was merged. On the combination the unity build,
16/16 targeted switch cases, sanitized plan contracts and 219/219 arm64 run
tests (one existing xfail) and 214/214 x86 run tests (six reference skips)
pass. The file-by-file build found an inherited visibility miss in the
autozero code: `stmt.c` called `wk_store`, local to `expr.c`. The helper is
now VPRIV with a prototype in walk.h. After the fix the file-by-file build
works and passes all 164 rule/level/target combinations with execution, and
the x86 debug check O0-O3. The measurements above explicitly refer to the
compiler code of fded15a + 19c6a63, not to the separate changes of the main
update.

### OPT8 status: unsigned32 constant division (2026-11-11)

`codegen/opt/udivconst.c` builds a typed 32-bit reciprocal with a fixed
multiplication/shift and, when needed, an add correction. The construction
has been checked against Hacker's Delight chapter 10 and
[libdivide's reference generator](https://github.com/ridiculousfish/libdivide/blob/master/libdivide.h),
with separate clang23 probes. Divisors larger than 2^31 need only an
unsigned comparison. Zero, powers of two, signed, 64/128 bits and `_BitInt`
keep their existing paths. This is a bounded u32 substep, not all of
OPT8/O2.

Separate adapters in `arch/a64/codegen/udivconst.c`, `codegen/walk/udivconst.c`
and `arch/wasm/codegen/udivconst.c` use the existing 32→64 multiplication; no
SSA/IR, allocator or new function pass. O1-O3 use the rule; O0 and
`VCC_NOOPT` are byte-identical to the base for all four targets. Native
normalizes lazy narrow values before the 64-bit product and keeps the
original for the remainder or correction. The actual register peak is
checked before allocation: one constant plus a possibly saved
original/correction, and one more register if the input is still an
address. Pressure keeps the ordinary division. Wasm chooses only the
simple reciprocal quotient and the comparison quotient; add correction and
remainders explicitly stay in `div_u`/`rem_u` until their larger sequences
pay off. The chosen wasm recipe is passed on without a second host
division.

`vcc-udivconst.py --sanitize` checks divisors 2-65 for all n<65536, 4096
seeded full-width divisors, boundaries around powers of two/multiples and
known reciprocals for 3/7/10, against independent division and modulo. The
contracts also test both native adapters' register budget at 0-8 free
registers, address/register input, correction/remainder and reserved
registers. `unsigned_divconst.c` runs normal/lazy-cast input, converted
divisors, 17 divisor classes, compound /= and %=, side effects, volatile and
index++. A deeply nested address expression tests native pressure; arm64
keeps udiv there. Unity and file-by-file build; 180 rule/level/target
combinations run, 220/220 arm64 regressions (one existing xfail), 215/215
x86 regressions (six reference skips) and debug O0-O3 pass. The
file-by-file binary also runs all 16 new u32 cases.

Measurement base: exact 781ca69; identical clang23 builds
`-O3 -march=x86-64-v3`, Callgrind 3.26 for the whole `vpp --target=... -O2
-S` process. Corpora of 500 functions for straight integer code, a
summation loop and deliberately saturated `x/7u + x%10u` code:

| Target | Straight code, Ir | Loop, Ir | Saturated division, Ir |
| --- | ---: | ---: | ---: |
| x86 | +0.0050 % | −0.0019 % | +4.32 % |
| arm64 | +0.0263 % | −0.0041 % | **+19.54 %** |
| wasm32 | +0.4416 % | +0.0067 % | +1.06 % |
| wasm64 | +0.4410 % | +0.0067 % | +1.06 % |

All eight non-applicable control/loop assembler pairs are identical. The
saturated arm64 cost is a real trade of compilation for more expensive
comparison/multiplication sequences: two udiv per function disappear, but
the number of emitted instructions increases. Wasm keeps both operations in
exactly the saturation probe according to its cost policy. A separately
configured mix of existing C regressions, with the same
c23/resource/runtime-include flags as the earlier OPT5 measurement, gives
the following instruction work:

| Target | Base Ir → new Ir | Difference | Files |
| --- | ---: | ---: | ---: |
| x86 | 132 564 698 → 132 542 609 | −0.0167 % | 12 |
| arm64 | 141 117 374 → 141 112 147 | −0.0037 % | 12 |
| wasm32 | 87 119 802 → 87 135 002 | +0.0174 % | 10 |
| wasm64 | 97 155 779 → 97 165 511 | +0.0100 % | 11 |

The wasm32 base's `atomic_ops` (layoutassert) and `bitint_runtime`, and the
wasm64 base's `bitint_runtime`, do not compile with this configuration and
are not included; the exact diagnostics are saved. The same types/files are
supported on native.

The x86 run probe of the saturated arithmetic class, 3 million LCG inputs
per process, two warmups and nine alternating pairs, gives a median child
CPU of 7.374→3.277 ms (−55.56 %); both answer 3967102618 like clang. This
measures exactly this host and arithmetic class, not arm64 hardware. For
wasm, clang23's actual O2 output was examined: `/3u` keeps `i32.div_u`. A
separate Node v26.13.0 probe with prewarmed modules, nine alternating pairs
and 3 million LCG inputs for each of 3/5/10/11/2147483649 gives −1.5…−5.1 %
median wall time with `--no-liftoff` and −28…−31 % with `--liftoff-only`.
All answers are equal; the verified object code is unchanged after the
recipe/pressure fixes. The results motivate the bounded wasm class on this
engine/host; no other engines or general performance promises are included.

Commands, sources, hash values and raw data are in
`work/opt-profile/udivconst/`: `compiler-profile.json`, `mixed/result.json`,
`native-runtime-profile.json`, `runtime-{turbofan,liftoff}.json` and the
respective `*-measure.py`/`runtime.js`. Signed and u64 constant
reciprocals, cheaper wasm remainder/correction, tail calls, broader G5/G7
gates and full O2 coverage are still open.

### OPT6: shared pure control decisions (substep)

`codegen/opt/control.c` proves constant integer conditions, logical negation,
short-circuiting and `__builtin_strcmp` between pure narrow string literals.
The shared visitor stops after at most 64 nodes and never visits a dead right
operand. Unknown calls, volatile/atomic, statements and inlining are left to
the existing generation; no SSA or AST mutation is added.
`__builtin_constant_p` uses each target's existing policy through a
callback. Arm64's callback keeps its existing dry generation with reset,
and x86's earlier double formula in the builtin/condition path is merged.
The shared proof is called once at each actual condition entry, not again
through the shared range helper `cond_fold`.

New decisions apply at O1-O3 with optimizations active. Eight focused
source files are byte-identical to `781ca69` for both O0 and O2 with
`VCC_NOOPT=1`, on x86, arm64, wasm32 and wasm64: 64 comparisons. The native
targets' old O0 rules for string comparisons are preserved. The new
run/code-shape probe passes 16 level/target combinations, also
file-by-file, and checks exactly one evaluation, preserved global
volatile/atomic loads, ordinary user-defined strcmp calls, and high string
bytes and an embedded NUL. Nine existing native tests for
constant_p/inlining pass. Wasm32/64 pass both `if_call_const` and
`cond_dead_and`; only these two xfails are removed. The other six reviewed
gaps still need work on switch segments, value facts for statement
expressions or inlining.

The cost is measured against exact `781ca69`, equal host flags
`-O3 -march=x86-64-v3`, the whole process in Callgrind, 500 functions per
probe, O1/O2. The pure scalar controls are practically unchanged. The
pointer loop's cost is about +0.094 % x86, +0.065 % arm64 and −0.002 %
wasm. A positive short-circuit/constant_p probe reduces host instructions
by about 16.90 % x86, 2.75 % arm64 and 18.15-18.36 % wasm. This is a
bounded compile cost, not hardware cycles or a full gate. The first probe
variant with a null callback on native gave extra work without gain and was
replaced; its profiles are saved separately. A broader performance gate, CI
and full O2 coverage are still open.

Final verification of the same implementation: 164 existing
rule/level/target combinations pass with execution. The new 16
combinations also pass after the file-by-file build. Raw profiles,
commands and source/binary hashes are local in
`work/opt-profile/control-final-cost/result.json`; the O0/noopt comparisons
are in `work/opt-profile/control-parity/result.json`. The general algorithm
does no extra whole-tree analysis for accepted proofs; wasm's existing
fallback for other forms is intentionally kept.

After integration of `54580f7` (PR48's regression base) unity builds and
all 16 new level/target checks pass again. The whole native suite gives
216/216 x86 PASS with six visible reference exceptions and 221/221 arm64
PASS with one existing XFAIL that ran. Logs are in
`work/opt-pr-tmp/control-integrated-{build,x86,arm}.log`.

### OPT5 regression base: generated memory cells (2026-11-11)

`src/tools/gen-memory-cells.py` deterministically creates 12 functions with a
total of 384 operation groups and 24 literal checksums from a separate
Python interpreter with explicit width masking. Six functions keep private
32-bit cells; six cover address escape through noinline helpers. Narrow
cells, source register changes, control flow, unions' floating-point payload
and volatile accesses are included. CI checks that the checked-in C file
can be regenerated; the ordinary run suite finds the file automatically.
Run results cannot prove that unread volatile stores remain; that requires
the separate `vcc-slot-observable.py` contract. This is a bounded
regression base, not a performance proof or full O2 coverage.

Verification: O1/O2 on x86, arm64/QEMU and wasm32/64 gave 192 matching
literal checks; Clang ASan/UBSan and the generator's `--check` pass. The
struct's fields are initialized separately: aggregate initialization and
array indexing create conservative address escape in the current adapters.
A positive O2 case on the same experimental x86 binary with slots off/on
goes from 88 to 77 frame moves (382 to 378 instructions). The corresponding
wasm32 case goes from 46 to 32 i32.load and from 31 to 29 i32.store against
the earlier reference binary. The last is a bounded comparison between
binaries, not an isolated cost.

OPT5 shared cell classification: em_slot_collect gathers ranges in a single
stream. The first exact range is saved when an allocation goes from one to
several ranges; all following ranges go directly to the shared
classification. Callers need neither replay the records nor create a
separate index buffer. Do not mix collection modes after the transition to
several ranges. ASan/UBSan: the same deterministic range oracle for both
API modes, 400 random scratch reuses, sort limits 15/16/17, overlaps, ID0
poisoning, escapes, duplicated first ranges and invalid IDs/sizes. The
mutation that drops the first range is rejected. x86 is used first;
arm64/wasm can switch to the helper separately with parity and cost checks.


OPT5 streaming arm64/wasm collection (2026-11-11): both adapters now use
`em_slot_collect`; the second full pass over records/events disappears. The
helper validates known ranges once and updates the allocation status
directly; ID0 uses unchanged physical poisoning. Classification, value flow
and printing are unchanged. Separate backend files are kept.

Matched v3 measurement (`-O3 -march=x86-64-v3`, whole-process Callgrind Ir,
O1/O2, base 781ca69 against 54580f7 + these changes) gives, for two
fields/500 functions, arm64 −0.294 % and wasm32/64 −0.160 %; for 16
fields/100 functions −0.453 % and −0.145 % respectively. One field/500
functions costs +0.053 % and +0.050 % respectively. Scalar control
+0.006-0.007 %, loop control arm64 +0.071 %, wasm +0.006 %. This is
instruction cost at compile time, not run time. The first access+collect
variant and an intermediate variant were rejected: wasm increased on the
large case (+0.246 %). The final version is kept because the multi-field
cases improve on both targets without a large single-field regression. Raw
data, exact commands, source/binary hashes and byte parity are local under
`work/opt-profile/stream-collectors-{direct-cost,direct-large-cost,parity}`;
the rejected `stream-collectors-{cost,first-range-cost,large-cost}` remain.
The base's compiler files match 54580f7; only PR48's test fixture differs.

Verification: ASan/UBSan for the random range oracle in both API modes and
the mutation that drops the first range; 118 byte-identical pairs (O1/O2,
four targets, including 24 arm64 objects), separately observable
volatile/atomic accesses with mutation and wasm32/64 cell tests O0-O3. Two
wasm32 atomic_ops pairs are rejected identically by the base because of the
test's LP64 assumptions and do not count as passed. The unity and
separate-file builds with j2 pass, as does the value state's sanitizer
contract. No new emission rule or full O2 coverage; the full performance
gate remains.


OPT5 physical poisoning, small follow-up: when the two most recently
collected ID0 ranges have exactly the same offset and size, only one is
saved. Classification uses the union of the ranges; validation still takes
place before the comparison. No new buffer or status is introduced. The
random oracle now contains repeated poison runs; a second mutation that
ignores the size is rejected by a same-offset/wider case. ASan/UBSan, both
mutations, 118 byte-parity pairs and the observability contract pass.
Against the previous d870b64, a bounded repeated volatile range reduces the
compilation's Ir by 0.042 % on wasm and 0.039 % on arm64; alternating
ranges without a hit cost 0.116 % and 0.105 % respectively. Earlier
controls and multi-field cases are practically unchanged. Both workloads remain
in `work/opt-profile/stream-collectors-prune-poison{,-repeat}-cost/result.json`;
the final version's full base comparisons are in
`stream-collectors-prune{,-large}-cost`. A separate x86 integration reports
13 397 fewer Ir in its real gate; it is not a full gate for this
arm64/wasm change. No emission change.

### OPT4 continuation: budgeted flow core and first x86 consumer (2026-11-11)

`codegen/flow/{graph,maskflow}.c` builds validated block adjacency with
optional predecessors and solves backward 64-bit may-liveness. Source
positions, registers/effects and rewrites belong to the adapter; the core
assumes neither machine records, a physical register model, the wasm stack
nor SSA. Arrays are reused per worker. Each block/edge visit charges an
explicit work budget; the result is valid only after full convergence.
Incorrect edges, allocation failures or budget exhaustion give no dependent
rewrites.

The first benefit is in `arch/x64/codegen/dead.c`: an absorbing integer rule
that leaves an already emitted register result unused sets a cheap candidate
flag. Without it no graph is built. The consumer removes only pure,
non-trapping register operations all of whose outputs are dead. Reads,
stores, atomics, FP effects, calls and barriers are preserved. Flag reads
are propagated conservatively backward; a flag write is not assumed to kill
the whole flag state. Partial registers and implicit registers are taken
from the target's existing effects. The epilogue's ABI results, frame/stack
bases and callee-saved registers are conservatively live at exit.

The limits are 256 blocks, 1024 edges and 65536 records; the solver budget
is `n + 32*(block+edges) + 64` visits. These are the implementation's
fallback limits, not a measured compiler cost gate. Unknown/indirect flows,
raw/named asm, label addresses and interrupt/noreturn conventions abstain
entirely. All changes wait until the analysis has succeeded; compaction
uses the existing `dbg_remap` before the two-address pass. Arm64's existing
block/liveness analysis is unchanged. At this first checkpoint wasm does
not yet use the graph; the separate wasm consumer is described in the next
status section.

Code-shape proof against the exact updated parent `0dde275`: `unsigned f(unsigned
x) { return (x*13u)&0u; }` keeps `imull $13` in the base but not in the
candidate at `7f88daa`. Protected stack reads remain. This shows a real
bounded gap/gain, not general DCE or measured run time. The base binary's
SHA256 is `a26750515e4e215f6485e3f75624a5422d9b29b25d9268bb81c9e3dfbb2fc8c6`;
assembly and build/test logs are in `build/linux/flow-evidence/`. The host
flags differ between these builds, so they are not used for cost
comparison. Later main integration and its verification are reported below.

Verified so far: unity build with `-j 1`; `--rule deadresults --run` passes
16/16 level/target combinations (x86, arm64/QEMU, wasm32/64 with Node
26.13). Positive x86 code requirements apply at O1-O3; controls keep
division, volatile access, calls and the opaque-asm fallback. `vcc-flow.py`
compares against an independent bitwise path-reachability model, including
loops, joins, duplicate/self edges, exact/empty budgets and reuse after
failure. `vcc-rec-dead.py` tests real record effects, ABI, flags/carry,
partial registers, the compaction/debug callback and the transactional
budget/opaque fallback. Both contracts pass ASan/UBSan and are in the fast
CI gate.

The naked probe is code-shape material: a valid asm body is opaque and
abstains. Vcc's existing ignored naked attribute gives the wrong prologue
for a direct asm ret; this was reproduced even without the new pass. The
probe therefore does not run the unsupported function. Clang 23 rejects the
corresponding pure C body with `non-ASM statement in naked function is not
supported`. Ordinary asm with absorbing expressions is however tested both
in code shape and at run time.

After integration of the updated PR53, leaf50 and the ledger (`7691f6c`,
parent `69c1bd7`) both unity and file-by-file build with `-j 1`. The
file-by-file binary also passes all 16 new rule/level/target combinations.
The controls for carry, partial register, overflow, loop and both ABI result
pairs now contain actually evaluated absorbing expressions, so the new
consumer is activated; selected controls also require that the extra
multiplication really disappears. A separate ordinary function without a
candidate stops without graph/scratch allocation in the contract test.

The own x86 suite gives 223/223 PASS and six explicit reference skips; arm64
228/228 PASS and one existing XFAIL (`--no-cache`, `-j 1`). Existing
cross-compiler ABI fixtures give 32/32 on each native target. The debug
harness has an actually activated dead-result case with a following source
line; O0-O3 pass the line requirement, DWARF verification and identical
`.text` with/without `-g`. Against the exact `69c1bd7`, 17/17 assembly
pairs are byte-identical: four targets at O0 and O2/noopt, and
arm64/wasm32/wasm64 at O1/O2/O3. The same parent still keeps `imull $13` in
the positive x86 probe. Raw logs are in
`work/opt-pr-tmp/flow-final-{x86,a64}.log`; other artifacts, complete
parity commands and source/binary hashes are in `build/linux/flow-evidence/`.

Cost against the exact `69c1bd7`, both builds with Clang 23
`-O3 -march=x86-64-v3`, Callgrind for the whole `vpp -S` process. 21 serial
pairs; the measure is host instructions, not hardware cycles or wall time:

| Corpus | Target/level | Changed instruction work | Generated assembler |
| --- | --- | ---: | --- |
| Straight code, 500 functions without candidate | x86 O1 and O2 | +0.0468 % | Identical |
| Summation loop, 500 functions without candidate | x86 O1 and O2 | +0.0009 % | Identical |
| Deliberately saturated dead-result diamonds, 500 functions | x86 O1 and O2 | **+7.7486 %** | Dead multiplications disappear |
| Twelve existing C regressions, summed | x86 O2 | +0.06964 % | 10 identical, 2 changed |
| Straight control code, 500 functions | arm64 O2 | +0.0221 % | Identical |
| Same control code | wasm32 and wasm64 O2 | +0.0187 % | Identical |

The mixed suite costs 140 485 975 → 140 583 816 instructions, with no
excluded files. It uses the earlier `deep_mixed`, `register_locals`,
`bit_cast`, `stack_cells`, `integer_identities`, `deep_conditional`,
`unsigned_pow2`, `signed_pow2`, `atomic_ops`, `mem_builtin_inline`,
`bitint_runtime` and `wasm_frame_cells`, not the new positive fixture. The
tradeoff is real: saturated candidate code costs about 7.75 % more to
compile; the control/mixed probes are under +0.1 %. No general runtime
gain, memory gate or complete performance gate is claimed. The run is an
instruction count under shared machine load, not a time comparison. Raw
`.cg`/logs/assembly, version strings, complete commands and source/binary
hashes are in `work/opt-profile/flow-cost/{measure.py,result.json}`.

Remote CI and the next wasm consumer remain. OPT4/O2 and G5/G7 are still
open.

### OPT5 continuation: wasm DSE across structured joins (2026-11-11)

On `1d8e29a`, with the flow parent `7691f6c`, wasm32/64 now also use the
shared graph/mask solver. `arch/wasm/codegen/flow.c` registers typed block,
if/else, end and branch events from the start of the function during
ordinary emission. Each opening gets a unique token; wasm's reused lexical
branch depth is translated to a token when the branch is emitted. No
assembler text is interpreted, and no extra AST walk or SSA is introduced.

The first consumer removes stores to private exact 4/8-byte frame cells that
are overwritten before every following read, even through nested diamonds.
Integers and floating point are supported; the existing
cell/overlap/escape analysis decides eligibility. The address and the right
side are still evaluated by the existing renderer, including any captured
wasm locals. Pure control boundaries break the local forwarding but are not
memory reads in DSE. Real call/memory barriers are merged only within the
same control epoch: a barrier after an empty else/join must not be moved to
only one of the predecessors.

Liveness unions the successors' read needs and regards all cells as read at
function exit. This is thus overwrite DSE, not general
unread/end-of-lifetime DSE or global value forwarding. All decisions wait
for full convergence and are published only after separate staging. Loops,
switch/dispatchers, goto, asm, SJLJ and dynamic frames abstain. Limits:
8192 control events, depth 256, 64 cell bits and at most 1048576 solver
visits. Graph/solver work starts only after complete emission and real
private repeated stores through a join. O0/noopt abstain; errors and budget
exhaustion leave this consumer's rewrites unpublished.

Verified: unity build with `-j 1`, standalone syntax check of all changed
wasm C files and **114** SDK-free configurations through the own
assembler/linker and Node: 10 new flow checks (wasm32/64 O0-O3 with
execution and code shape, and O2/noopt code shape), 16 existing cell cases
and 88 opt-rule checks. Positive cases keep exactly one final store;
negative cases cover reads/exits, escape, overlap, volatile/atomic, calls,
effects, floating-point bits and excluded flow. An isolated mutant that
removes the epoch condition is rejected by the intended empty-else code
check on both wasm32 and wasm64; the original sources were not changed.
Independent review found no remaining correctness blockers.
`vcc-wasm-flow.py` is in the wasm job's fast CI set; remote execution
remains.

The whole own SDK-based suites on the same `1d8e29a` end with status 0:
wasm32 **178/178 PASS**, 36 executed XFAIL and 16 skips (12 clang-reject,
1 noref, 3 ref-fail); wasm64 **182/182 PASS**, 37 executed XFAIL and 11
skips (8 clang-reject, 1 noref, 2 ref-fail). The exceptions are not PASS.
Logs: `work/opt-pr-tmp/wasm-flow-full32.log` and `wasm-flow-full64.log`;
source evidence, mutant hashes and focused results are in
`build/linux/flow-evidence/`. After merging the exact flow parent
`f7327bd` into `1458a90`, all 114 configurations and the standalone syntax
check of changed wasm C files pass again. 66 O0/O2-noopt pairs are
byte-identical to that parent; two further pairs are rejected by both
builds' existing ILP32 layout assert in `atomic_ops`. The SDK-based
full-suite figures above still belong to `1d8e29a`, not the later
integration.

Bounded cost comparison: exact `f7327bd` against `1458a90`, both vpp built
by clang 23 with `-O3 -march=x86-64-v3`. Callgrind counts the whole process
including startup, one serial measurement per binary/source/target, wasm32
and wasm64 with `-O2 -S`, explicit LLVM resource/rt headers and the same
source files. No wall-clock or program run-time values are used.

| Probe | wasm32 extra host Ir | wasm64 extra host Ir |
|---|---:|---:|
| Straight scalar code, 200 functions | +0.314 % | +0.314 % |
| Conditions without private frame cells, 200 functions | +0.552 % | +0.552 % |
| Loops that abstain from analysis, 200 functions | +0.351 % | +0.351 % |
| Positive diamonds, 200 functions | +4.495 % | +4.494 % |
| Positive nested diamonds, 200 functions | +4.683 % | +4.682 % |
| Existing fixed mix, sum of the processes | +0.106 % | +0.095 % |

The controls' assembler and objects are byte-identical. The positive probes
reduce 800 and 1000 stores to 200; a separate ordinary `-c` gives
2.886-3.325 % smaller relocatable objects in total. This is object size,
not loaded code size or measured program speed. Active analysis thus costs
more than 1 % in these concentrated positive probes; the table does not
count as a general cost gate. The fixed mix is eleven existing regression
files for wasm64 and ten for wasm32: `atomic_ops` is rejected by both base
and candidate on its LP64 layout assert in wasm32 and is openly excluded.
Two of the mix's assembler outputs change per target.

Full commands, source/binary hashes, raw Callgrind files, objects and error
texts are in `build/linux/flow-evidence/cost/result.json` with adjacent
artifacts; the parity in `noopt-parity.json`. These measurements belong to
this consumer and do not replace a full performance gate, program run-time
measurement or remote CI. OPT5/O2 and G5/G7 are still open.

### Continued O0-O2 coverage: family ledger after OPT0-OPT8 (2026-11-11)

**OPT0-OPT8 are first substeps, not the whole O2 goal.** This map binds
the continuation to real inventories and source code in the private
combination `da64f05` (no new compiler measurement in this document step).
`bench/optaudit/coverage-ledger.json` groups **every** distinct entry in the
report's GCC `enabled_options` and LLVM `observed_passes`, O0/O1/O2, exactly
once into 20 families. It has full names, level membership, concrete code
files per target, family status and remaining work. It is a checked-off
*inventory mapping*, not full optimization coverage.

Basis: `work/opt-profile/audit-switch-integrated/report.json`, SHA256
`cb92d9f847cc89ce574ef6fccc91c2ad8ab862a0989f0fa19c76cf607a1658ca`.
The report's vcc revision is `54580f7`, thus **before** the code combination
above; its assembly measurements must not be relabeled as `da64f05`. The
references are GCC 15.2.0 (Ubuntu 15.2.0-16ubuntu1) and Ubuntu Clang 23.1.6
(build revision c47d7823685a), on a Ryzen 7 9700X. The same 29 C functions
are used with `-march=x86-64 -fno-pie -fno-stack-protector
-fcf-protection=none`. The SHA256 of the probe/caller sources, the vpp
binary hash, the full version string and the SHA256 of reviewed compiler
sources are in the annex.

| Inventory | O0 | O1 | O2 | Union O0-O2 |
| --- | ---: | ---: | ---: | ---: |
| GCC: boolean options marked `[enabled]` | 50 | 92 | 141 | 142 |
| LLVM: unique observed `Running pass` names | 6 | 74 | 87 | 88 |

An enabled GCC option does not mean that the corresponding transform ran or
changed anything; even O0 lists several such options. Names such as
`-fno-inline-stringops`, FP/alias rules and unwind are moreover policy. An
observed LLVM pass can be an empty pass, a wrapper or metadata handling.
The report's GCC `configured_passes_on` is a separate inventory and is not
part of this name mapping. Parameter-controlled GCC choices, full machine
passes, run order/count, other ISAs, O3, LTO and PGO are not inventoried
here. The backend columns below are source review, **not** new GCC/Clang
inventories on arm64/wasm or new run evidence.

In the table, **partial** means that a real bounded implementation exists,
not that all flags/passes of the family are covered. **Missing in general**
means that no corresponding general analysis/transform was identified in the
reviewed code path; **unverified** requires targeted probes before a claim
of support or absence is made.

| Family, examples from the inventory | Actual basis in vcc and remaining gap |
| --- | --- |
| Integer simplification: InstCombine, reassoc, div/rem | **Partial, all three.** `opt/{identity,divpow2,sdivpow2,udivconst}.c` with separate adapters. U32 reciprocals have target cost/fallback; not general InstCombine. Small constant multiplications, more bit rules, shared quotient/remainder recipe and signed/u64 reciprocals remain. Float2Int is not proven by the integer rules. |
| Value facts, CSE/PRE, ranges: CCP, GVN, SCCP | **Partial, local.** `rec/copies.c`, `rec/slot-values.c` and prepass facts; a64's `opt.c`, x64's `copies.c`/`extensions.c`, wasm's `slots.c`. No shared global GVN/PRE/SCCP or general range analysis. Start with a bounded block-local cache for pure expressions. |
| Dead code and memory: DCE/DSE, store merging | **Partial.** Shared exact frame cells/store facts and reachability cleanup; a64 has its own dead-result passes, x64 effect description, wasm typed access events. Heap/global DSE, bitwise DCE, allocation elimination, general load/store motion and loop load elimination remain/are unverified. |
| Control and switch: SimplifyCFG, jump threading, bit tests | **Partial.** `opt/control.c`, `opt/switch.c`, `rec/{fallthrough,unreachable}.c`; all targets use shared decisions, bounded membership masks and value tables, wasm has a dense `br_table`. Bitset clusters with several destinations, general threading, compare merging and tail merging remain. |
| Scalar replacement/promotion: SROA, mem2reg | **Partial.** `opt/locals.c`, prepass and cells; a64 register binding, x64 four chosen GPR homes, wasm typed locals. This is not general aggregate splitting or join-aware mem2reg. |
| Loop motion/induction: LICM, IV opts, loop sink | **Missing in general.** All targets lower loops; the prepass's loop-weighted local selection is not LICM. CFG, dominance, loop/dependence facts and zero-iteration proofs are needed before safe hoisting/IV rewriting. |
| Loop shape: rotation, deletion, unroll, unswitch/distribute | **Missing in general.** The targets have direct statement lowering, not these transform passes. Requires trip-count/effect/entry proofs and a code-size budget; possibly start with small exactly constant loops. |
| Vectorization: loop, SLP, VectorCombine | **Partial, explicit only.** A64 `expr.c:gen_vbin` has selected NEON operations plus lane fallback; the x64 walker and wasm `wexpr.c` use a lane-wise path for reviewed GNU vectors. Packed SSE2/simd128 lowering is a separate earlier step. Automatic loop/SLP needs a dependence, alignment, reduction, tail and cost model. |
| Inlining/calls: early/partial inline, TailCallElim | **Partial, asymmetric.** A64 `call.c:can_inline`, x64 `walk/stmt.c:wk_can_inline` have different eligibility; x64 already supports small ordinary static functions. Wasm `wcall.c` lacks a corresponding inliner. Shared policy, wasm instantiation and bounded tail calls remain; general varargs/partial inline is later work. |
| IPA/global: IPSCCP, attrs, global DCE, ICF | **Bounded prepass facts; general IPA missing.** `prepass.c`/`tu.c` and native inlining are not whole-program analysis. Call graph, linkage/interposition and escape/modref summaries are needed. The linker's section GC is not compiler GlobalDCE or ICF; C++ devirtualization is a different language question. |
| Library/idiom: strlen, CRC, MemCpyOpt | **Partial.** Shared literal `__builtin_strcmp`, selected native memory builtins; wasm has its own call path. Loop idioms, general memcpy chains and strlen/printf/CRC simplifications are not established. Keep the freestanding, builtin, bounds and overlap contracts. |
| Register policy: IRA/LRA, remat, caller saves | **Partial, different model.** Shared local selection, a64 `bind.c`/`opt.c`, x64 `locals.c` and temporary pools. No general live-range splitting/IRA/LRA identified. Wasm locals are VM locals, not a requirement for the same physical allocator. |
| Machine combining/addresses: peephole, compare elim, REE | **Partial.** A64 arithmetic/ext facts, x64 `lower.c` and the last-record-u32 fact in `extensions.c`; wasm stack/expression lowering. Shared address/constant recipes can help, but SIB/NEON/SSE/flags are target mechanics. General compare/combine parity is unverified. |
| Layout/scheduling: align/reorder/sched, CGProfile | **Unverified policy; general scheduler missing in the review.** `tu.c` and the targets' printers have ordering/alignment; this proves neither profile-based placement nor latency scheduling. Start with likely branches and measure code-size/fetch. Wasm needs its own structured policy. |
| ABI/frame/debug/security: omit-frame, shrink-wrap, unwind | **Partial policy.** X64 `frame.c` proves frameless leaf; the targets' prologue/CFI have their own contracts. Not general shrink wrapping. Stack-clash, assignment tracking, instrumentation, PLT/interposition are separate user/correctness requirements; a GCC default in the inventory does not prove vcc flag support. |
| FP semantics: signed-zero, errno, trapping | **Policy, exact flag parity unverified.** `frontend/c/const.c`, `include/common/f128.h`, native `fp.c` and wasm FP lowering exist. These flags are mainly restrictions on allowed rewrites, not proof of fast-math or permission to reassociate FP. |
| Alias/alignment/assumptions: PTA, strict-alias, InferAlignment | **Partial, exact objects.** Prepass/escape/cell overlap exist on the targets, not general TBAA/points-to/global AA. Extend object/disjointness facts before memory motion; language policy and char/union/overlap must be tested separately. Analysis invalidation is infrastructure. |
| Pipeline/metadata: annotations, intrinsic lowering, remarks | **No one-to-one gap.** LLVM's pass manager, metadata and representation lowering do not require the same vcc passes. AllocToken/MemProf and intrinsics must be tied to real source semantics before any implementation. |
| Other languages: Coro*, OpenMP* | **Outside this C probe's transform evidence.** That the pass names ran says nothing about coroutine/OpenMP optimization in the probe. Any language support is planned separately. |
| Atomics/unreachable policy | **Partial lowering; flag parity unverified.** `frontend/c/builtin.c` and target adapters have atomic paths. Inline/libcall choice, ordering, feature fallback and unreachable-trap policy need their own concrete contracts; they are relevant C requirements. |

**Next implementations, in cost order:**

1. **Cheap shared recipes without new IR/SSA.** Finish the source-near
   OPT6/OPT7: unused *pure* local initializers, small multiplications/bit
   rules, typed scaled addresses and a constant-store hook. Use existing
   prepass facts and O(1) decisions, not repeated AST search. Add bounded
   switch value tables/bitset clusters where the target shows a gain.
   Explicit packed vector lowering uses shared semantics but the target's
   instructions/features; it does not require an autovectorizer.
2. **Bounded local value facts.** A fixed-size cache per block for pure
   expressions/constants, with register versions, exact clobbers and clear
   invalidation. Reuse the cell core for small non-escaped aggregate
   fields. Preserve trapping/volatile/atomic and implicit flags/asm.
   Quotient/remainder sharing and rematerialization are tried on concrete
   pressure cases, not as new whole-function passes without measured benefit.
3. **Calls with target contract.** Shared bounded inline eligibility and
   cost, then wasm's own parameter/result/scope mechanisms. Introduce
   bounded sibling calls with ABI, frame-escape, cleanup and unwind proofs.
   A shared policy alone delivers no wasm inliner/tail call.
4. **Shared CFG before global rewrites.** Extract reusable blocks/edges,
   complete label entry and conservative liveness without degrading a64's
   existing analysis. Set a work budget for dominance and dataflow; budget
   exhaustion gives fallback, never unfinished safety proofs. Then
   join-aware constant/range propagation, branch threading, dead-result/DSE
   across blocks and the first safe loop invariants.
5. **Advanced O2 in separate family steps.** Global GVN/PRE, aggregate
   promotion, loop/induction rewriting, autovectorization and IPA each get a
   correctness/code-shape gate and a measured CPU/memory budget. SSA is
   justified to evaluate for many join values, sparse global dataflow, GVN
   and loop rewriting; neither CFG, local CSE, AST inlining nor a simple safe
   LICM case automatically requires SSA. Auto-SIMD also needs memory
   dependences and target cost, not just a new representation.

Each family step requires a negative base case showing the gap, a positive
per-target code requirement and C result/effect checks on
a64/x64/wasm32/wasm64. Measure the whole compiler's extra work, avoid
passes when candidates are missing and report what is not covered.
Separate O1/O2 policies are introduced only when measurements justify them;
no level is renamed to claim parity. G5/G7 and full O2 coverage remain
open. This document step runs no builds or new benchmarks; the annex's
set/uniqueness/level/file-hash checks are data checks of existing material.


### OPT1: small constant multiplications with target cost (2026-11-11)

First bounded step implemented: `opt/mulconst.c` normalizes the constant to
32/64 bits and chooses, in constant time, shift 1/2/3 for the factors 3/5/9.
Separate `arch/{a64,x64}/emit/mulconst.c` use the recipe at O1+ and without
`VCC_NOOPT`: arm64 chooses the existing ADD with a shifted operand, x86 a
new pure scaled LEA record with both register uses and without flag/memory
effects. The 32-bit result is zero-extended before any later sign
extension. No extra temp, AST walk or operand evaluation is needed. Wasm
keeps mul, as Clang 23 does; a forced shift/add recipe would add operations.

A local audit against 69c1bd7 (arithmetic files identical to main 2dc4ea3)
and Clang 23.1.6/GCC 15.2.0 shows that this is missing on both native
targets. Arm64 already has multiplication by powers of two; zero/one exist
in the shared rule. The factors 6/7/10/15 are deferred: the references use
two operations where x86 has an immediate imul and arm64 two mov/madd;
broader target cost is required. A manifest with source/binary hashes, all
36 audit commands and assembler is local in
`work/opt-profile/multiply-audit/`. The base binary has SHA256
`61249ca189d71743da9c506100c3d3dc8451cb3338e2898bc904b03d7cda87f4`.
This is code-shape evidence, not measured run-time gain.

Verified before cost measurement: unity and file-by-file with j2, 16 fully
byte-identical assembler pairs between the builds, 16 target/level
combinations O0-O3 with literal run answers and intended code shapes. The
cases cover signed/unsigned 32/64, negative defined products, wraparound,
narrowing with dirty high bits, subsequent extension, *=, commuted
constant, a single pointer/value function and a global volatile load. 36
byte comparisons against the base preserve O0, `VCC_NOOPT` and the overflow
builtins. The already ignored optnone attribute gives no new promise.
ASan/UBSan for the recipe's modular oracle and mutation, and the x86
records' uses/definitions/flags/zero extension, pass. CI runs the new
recipe check and the existing opt-rules pick up the new case automatically.
Final integration CI remains; a bounded cost measurement is below. No SSA,
general InstCombine or full O2 coverage is implemented.


OPT1 matched cost for 3/5/9: exact clean candidate `f92b67b` against
historical base `69c1bd7`, both host builds `-O3 -march=x86-64-v3`,
whole-process Callgrind Ir and 500 functions per probe, O1/O2 for all four
target triples. A scalar control with factor 37 (the recipe is declined)
costs +0.1256 % x86, +0.1231 % arm64 and +0.0141-0.0142 % wasm. The
pointer loop costs +0.0863 %, +0.0041 % and +0.0080 % respectively. All
control outputs are byte-identical. A positive probe with the factors 3/5/9
costs +0.3529 % x86, −2.1465 % arm64 and +0.0160-0.0161 % wasm. There 500
imul become 500 scaled LEA on x86 and 500 materialized constant+madd become
500 shifted ADD on arm64; wasm keeps 500 mul and its assembler is
byte-identical. This measures compile work, not the program's run time or
the full performance gate.

The candidate binary's SHA256 is
`8492bdd0cb53f89d1ad3173ac48bcb837f668a20a4860b0b3fa64ee232ccf362`.
`work/opt-profile/mulconst-cost/result.json` preserves all 24 matched pairs,
exact commands, compile flags, source/binary hashes and code-shape counts.
`work/opt-profile/mulconst-unity-parity/result.json` preserves the 16
byte-identical build-mode pairs. Base 69 is deliberately historical; a
later integration must not describe these numbers as a measurement against
its new parent. Final integration CI and full G7 are still open.

OPT8 integration: the implementation is 58263be; after it main54580f7
(PR48's generated memory regression) was merged as 6b0e195. The conflict
concerned only this plan; both status blocks are preserved. The compiler
sources are unchanged by this merge. Unity, 180/180 targeted run/code
requirements and the sanitized reciprocal/pressure contracts pass on the
combination, as do 221/221 arm64 regressions (one existing xfail) and
216/216 x86 regressions (six reference skips). The measurements' JSON
states exactly 781ca69→58263be and `compiler-sources.json` binds the ten
changed compiler files with SHA256.

### Status OPT8 (2026-11-11): first bounded native tail calls, integration in progress

`codegen/opt/tail.c` proves the same simple wrapper form for both register
targets: the whole body is a direct return-call, identical 32/64-bit
integer/pointer types, arguments are own formals or typed literals, fixed
prototypes and at most the target's register capacity. No new whole AST
walk or SSA. The targets' own `tail.c` park arguments before ABI placement;
a terminal marker keeps all outgoing arguments live to the epilogue. X86
jumps after the restored frame/CFA; arm64 writes the symbol branch as JUMP26
even directly to an object and restores CFI before the branch. Stack
arguments, locals/address escape, cleanup/VLA, conversions, indirect calls,
floating point and general tail positions remain.

`-f[no-]optimize-sibling-calls` is interpreted in order and is part of the
cache identity. The attributes returns_twice, disable_tail_calls,
not_tail_called and optnone are stored/inherited; the new rule respects
them. Other passes do not yet have a full optnone policy. Standard wasm
keeps an ordinary call: clang 23 O2 does the same; return_call requires an
explicit tail-call feature and assembler support, which are not included
here. This is not an implemented wasm tail transform.

Checkpoint on PR50 base 10e2cb3: unity builds; 16 focused O0-O3
targets/levels pass code requirements and execution, native also CFI. Real
kernel evidence exists: preprocessor baseline 881/881 and the candidate's
sema 887/887 both per process and batch. This is historical evidence for
that base, not proof for the latest main. Updated integration `2506518`
includes PR50 `0a3c988` and main `f97d16d`. On that source sema is green
887/887 both per process and batch; real kernel parity is 881/881. Own x86
tests pass 223/223 with six explicit reference exceptions; own arm64 tests
pass 228/228 plus one existing xfail. ABI is 32/32 on each of x86 and
arm64. All four targets' O0-O3 tests run correctly; native also tests CFI.
Arm64's direct C-to-object has the right JUMP26/CALL26 relocations and runs
correctly under qemu. 48 mixed flag/attribute jobs with declaration cache
off/on are green. Frame regression, real signal unwind and register-home
unwind (ordinary and 64-aligned frame) are green. Changed C files pass
separate syntax compilation; the effect contracts are green. The fast cache
and direct-object tests are now part of CI.

Two cold 501-function profiles with exact base `0a3c988` and candidate
`2506518`, both built with `VCC_OPT='-O3 -march=x86-64-v3'`, show: simple
integer wrappers with one formal and one literal go from 17 934 254 to
15 767 383 compiler instructions (−12.082 %) and from 32 064 to 24 038
`.text` bytes. Non-qualifying arithmetic returns go from 17 211 605 to
17 269 849 (+0.338 %); the assembler and 24 039 text bytes are identical.
`work/opt-profile/tail-cost/report.json` contains commands, the hashes of
all tracked compiler sources, input sources and binary hashes;
`measure-tail.py` recreates the profiles. The base binary's SHA256 is
`d8e6b3b2bc693eb75374d41f4f7f3f871fba1aad78b1eb1a04bf5d65ba99d8cc`,
the candidate's `dcb4323027bc076c74904eb179d3a9575cb06224a114cf2f31e005fcbc7ad0c2`.
These are two bounded Ir profiles, not general compiler speed or program
run time; the machine was shared and no wall-time claims are made. G5/G7 and
the whole O2 goal are still open. General tail positions, stack/FP/aggregate
ABI and explicit wasm tail features remain.

Tail integration after main `103cdee` (#51/#60): both plan blocks and the
harness's tails/udivconst rules are preserved. Unity, the four targets'
O0-O3 tail tests, 48 mixed cache checks, native constant checks, both
natives' unsigned rules and the reciprocal contract pass. The whole CI was
green on `8532f90` before this merge. The kernel/suite evidence and Ir
profiles above apply to their stated source versions; the main integration
is not a new cost measurement.

### OPT8 status: unsigned64 reciprocal division (2026-11-11)

The substep from exact 046c6dc extends `codegen/opt/udivconst.c` with a
separate u64 construction in host u128, checked against libdivide's u64
generator above. U32 keeps its own cheaper construction. Native uses the
existing UMULH/MULW through a separate walker hook, saving the live implicit
RAX/RDX. The budget counts lazy input, magic and saved original/correction;
register pressure keeps the ordinary division. No SSA or new AST walk.
Wasm rejects ordinary u64 reciprocals before the construction and chooses
only the comparison quotient for divisor>2^63, like clang23's actual wasm
lowering. U64 remainder and low divisors keep wasm `div_u`/`rem_u`.

Sanitized contracts test 4096 full-width divisors, 32 random divisors per
exact width 2-63, power-of-two neighbors, extreme values and multiples
against independent division/modulo; the old u32 contracts remain. The
actual x86 hook is tested in 1536 register/alias/live-mask cases including
RAX/RDX. The run probe has 18 divisor classes, full-width input, multiples
±1, /= and %=, volatile, side effects once, fixed RAX/RDX and nested
address/register pressure. Unity and file-by-file build. 196/196
rule/level/target combinations pass on x86, arm64, wasm32 and wasm64, also
the 16 new u64 cases with file-by-file. 222 arm64 regressions (one existing
xfail), 217 x86 regressions (six reference skips), ABI 32/32 per native
target, libgcc unwind and x86 debug pass. The independent read review found
no remaining blocker.

Paired Callgrind measurements use exact 046c6dc against e1fdb3d's compiler
code, clang23 `-O3 -march=x86-64-v3`. On four targets the 500-function
controls and loop controls are at most +0.00021 % extra work. The existing
saturated u32 corpus costs +0.052…+0.122 %, with identical assembler. None
of these nor wasm's rejected u64 cases execute the host's `__udivti3`. A
configured unchanged 12-file native mix costs −0.00303 % on x86 and
+0.00082 % on arm64; 10 and 11 base-supported wasm files cost −0.00794 %
and −0.00462 %. The wasm mix excludes existing unsupported atomics/BitInt
cases per the JSON. This is bounded control/mix evidence, not the whole G7
gate.

The new saturated u64 corpus (`x/7ULL+x%10ULL`, 500 functions) costs
**+14.10 % on x86 and +17.99 % on arm64 to compile**. That tradeoff is
explicit: reciprocal sequences and the host-u128 construction do more work.
On x86 a separate loop with three million seeded full-width LCG inputs
computes the same quotient/remainder sum: two warmups and nine alternating
child processes per binary give a median CPU of 10.920→3.325 ms (−69.55 %),
with an identical clang-verified answer. This applies to this corpus/host
and includes process start; it is not a general speed promise. Arm64 has
static UMULH/remainder code verified but no measurement on real arm64
hardware. Wasm keeps its more expensive class according to the cost policy.

Sources, exact commands, binary/source hashes and raw probes are under
`work/opt-profile/udivconst64/`: `compiler-profile.json`, `mixed/result.json`
and `native-runtime-profile.json` with the respective `*-measure.py`.
Signed reciprocals, cheaper wasm u64/remainder/correction, broader register
allocation, full G5/G7 gates and full O2 coverage remain. The u32 status's
older +19.54 % saturated arm64 cost still applies to its separate base
measurement.

U64 after integration with PR51 `54c8f3f` and main `f97d16d`: compiler code
`d8957b3` builds unity/file-by-file and passes 196 rule probes, 16 separate
control probes, 229 arm64 PASS (one existing xfail), 224 x86 PASS (six
visible reference exceptions), ABI 32 per native target, unwind/debug and
sanitized reciprocal/register-pair contracts. File-by-file runs the 16 u64
cases on four targets; the O0/noopt assembler is byte-identical to the new
parent in eight comparisons. Both sides' plan status and main's x86 changes
are kept.

New paired measurements of exact `54c8f3f`→`d8957b3`, same clang23/v3 flags,
are separately in `work/opt-profile/udivconst64-integrated/` with a source
hash manifest. Control/loop is at most +0.000105 %; u32 +0.052…+0.121 %
with identical assembler. The saturated u64 corpus costs **+13.55 %
x86/+17.97 % arm64** to compile. The same fixed mix costs +0.00429 % x86
(12 files), +0.00065 % arm64 (12), −0.00458 % wasm32 (10) and +0.01954 %
wasm64 (11); the same base-support exceptions. The seeded x86 probe now
gives median 11.047→3.396 ms (−69.26 %) and identical run binary hashes/
answers as the historical comparison. The earlier +14.10 %/+17.99 % still
explicitly apply to 046c6dc→e1fdb3d. None of the measurements replaces full
G7 or gives a general run performance promise.

U64 after main2dc4ea3 via updated PR51head15551a7: unity build, all 196
targeted run/code requirements, sanitized math/register-pair contracts and
the x86 leaf-frame policy/cache/debug/hardening/signal-unwind pass again.
The reciprocal and high-product sources are unchanged by the integration.
The profiles' exact 54c8f3f→d8957b3 delimitation is kept; no new cost or
general speed claims are made for this integration of leaf frames.

### OPT8 status: signed32/64 reciprocal division (2026-11-11)

The substep on top of exact `f585591` uses `codegen/opt/sdivconst.c` for a
shared reciprocal for signed32 and signed64. Separate native adapters use
the signed high product, a possible addition of the numerator, an
arithmetic shift and a sign-bit correction for C's rounding toward zero. A
negative divisor only negates the quotient; the remainder is computed with
the positive divisor magnitude. Zero, ±1 and powers of two keep existing
rules. _BitInt, FP, pointers, vectors and other widths are rejected. Wasm
keeps its `div_s`/`rem_s`, as does clang23 for the same small probes.

The recipe is built only for a constant right operand at O1+ outside
`noopt`. Signed32 uses a host64 division; only signed64 uses host u128.
The pressure limit is at most two extra temporaries plus a possible
materialization of the input; insufficient room keeps the ordinary
division. x86's separate signed high product reuses the unsigned variant's
ownership and protection of RAX/RDX. No SSA, new AST walk or dynamic
allocation is added.

The equality case in the reciprocal's error bound is permitted without an
extra numerator addition. A full-width proof in the helper's comment also
covers INT_MIN. Thus `/3` gets the same short magic multiplier as clang
(`0x55555556` and `0x5555555555555556` respectively), without requiring the
same register/frame code. Regressions cover ±3/6/7/10/12/37, large
divisors, extreme numerators, neighbors of multiples, signed64 ±9/129, lazy
narrowing, compound lvalues and effects.

The current integration builds unity and the eight changed/new
implementations compile separately. Four targets × O0-O3 pass execution and
code requirements; four further O2-`VCC_NOOPT` checks pass. Sanitized math,
pressure and signed/unsigned register-pair contracts pass. The native
suites give 230 arm64 PASS (one existing xfail) and 225 x86 PASS (six
explicit reference exceptions). ABI gives 32 PASS per native target. x86's
debug lines O0-O3 and real unwind through ordinary/over-aligned frames at
O1/O2 pass. The O0/noopt assembler is moreover byte-identical to `f585591`
in eight target/policy comparisons.

Paired Callgrind measurements use new binaries from exact
`f585591`→`fbc7105`, both with clang23 and `-O3 -march=x86-64-v3`. The
500-function control has identical assembler and costs +0.0261 % x86,
+0.0270 % arm64. The loop control costs +0.00795 % and +0.00091 %
respectively. The wasm controls and the signed corpus have identical
assembler and change at most 0.000214 % in instructions. Deliberately
saturated native corpora with 500 functions (`x/7 + x%10`) show the cost of
the extra compiler work:

| Target | Signed32, compiler Ir | Signed64, compiler Ir |
| --- | ---: | ---: |
| x86-64 | +14.21 % | +17.79 % |
| arm64 | +30.03 % | +20.44 % |

The same fixed 12-file mix as the u64 step costs +0.00256 % x86 (12 files),
+0.00572 % arm64 (12), +0.000141 % wasm32 (10) and −0.000034 % wasm64 (11).
The base-support exceptions are visible: wasm32 atomic_ops/bitint_runtime,
wasm64 bitint_runtime; no new target exceptions have been added.

A separate seeded x86 run probe sums signed64 `/-37`, `%129` and signed32
`/3`, `%-7` for three million full-width numerators. All answers match
clang. Nine alternating child-CPU probes after two warmups give a median of
**18.217→6.633 ms (−63.59 %)**, including process start. It is a bounded
probe, not a general speed promise or a measurement on arm64 hardware.
Source/binary hashes, exact commands and raw data are in
`work/opt-profile/sdivconst/`: `compiler-sources.json`,
`compiler-profile.json`, `mixed/result.json`, `native-runtime-profile.json`
and `noopt-identity.json` with the respective measurement scripts. Full G7
and full O2 coverage remain.

Bounded flow after stack integration with signed/tail/u64 (`661d552`):
unity, sanitized flow/dead-result contracts, 16 target/level probes with
execution and x86 debug O0-O3 pass. Earlier full suites and Ir
measurements above apply to their exact stated versions; the integration is
not a new cost measurement.


### OPT6: unused scalar local initializers (bounded substep)

Implemented: `codegen/opt/unusedinit.c` shares eligibility and recipe between
a64, x64 and wasm32/64. The existing `SA_USED` from the prepass rejects
referenced objects directly; no new function walk, CFG or SSA is added.
Only never-referenced automatic integer objects of at most 64 bits are
covered. Volatile/atomic, cleanup, VLA/variably modified types, static
storage, address escape, `used`, asm registers and `_BitInt` keep the
existing path. O0, `VCC_NOOPT` and an explicitly known `optnone` function
are also rejected.

A candidate-bound integer list may visit at most 64 AST nodes. It excludes
memory indirection, division/remainder, shifts, FP, calls, builtins and
unknown nodes. A proven pure initialization expression is omitted.
Otherwise the integer expression is evaluated as a value and freed/dropped;
only the destination's store is removed. This preserves calls, reads and
internal conversions that the existing value emitter performs. Budget
exhaustion keeps the evaluation. Brace/zero-initialization records still go
through the initialization pass; they are never sent to the value emitter.
`side_effects()` is not used as proof of the absence of traps. Existing
AST/symbol data is not changed; the register local selection avoids
reserving a home for these unused objects.

Verified so far: unity and nonunity build and a standalone recipe contract
normally and with ASan/UBSan. The contracts cover budget 0/1/exact limit,
64/65 nodes, eligibility, attributes, unknown nodes, FP/memory/division
checks and initializer fallback. Small O2 assembler probes show that x64's
dead multiplication/addition/store and wasm32/64's
multiplication/addition/local disappear; a64's existing post-pass had
already removed the arithmetic in exactly this probe. This is not general
DCE or a complete solution of OPT6.

Matched short Callgrind measurement: exact `f7327bd` against `beb6e2b`, both
built with Clang 23 `-O3 -march=x86-64-v3`, 16 serial pairs without
exclusions, O2 only. Candidate-free 200-function controls/loops gave x64
+0.0551/+0.2239 %, a64 +0.0630/+0.1846 %, wasm32 −0.0001/+0.1325 % and
wasm64 −0.0001/+0.1324 % executed compiler instructions; all assembler was
byte-identical. 200 positive functions gave x64 −6.3725 %, a64 −5.2347 %,
wasm32 −4.1897 % and wasm64 −4.1878 %. a64 gave the same assembler but
avoided work that its post-pass previously performed. Four existing x64
regression files (`deep_mixed`, `register_locals`, `atomic_ops`,
`integer_identities`) gave 55 286 799 → 55 297 899 instructions
(+0.02008 %), all with the same assembler. Manifest, commands,
source/binary hashes and reproduction script are in
`work/opt-profile/unused-init-cost/{result.json,measure.py}`. This measures
the whole compiler process's user instructions, not hardware cycles, wall
time or the programs' run time. It is a bounded exact-parent probe; the
combined optimization stack's separate measurement follows below.

The combined cost gate (`bench/vcc-opt-cost.py`) passes: at most 1 %
increase per target, both against `4a839ba` before the stack and against
`7be5f64` before this substep. The candidate was `59c686c`; all three
binaries were built with Clang 23 `-O3 -march=x86-64-v3`. The series
measures Callgrind Ir for the whole `vpp -O2 -S` on a fixed corpus of
existing regression files, with the same candidate profile in both
comparisons:

| Target | Files | Against `4a839ba` | Against `7be5f64` |
|---|---:|---:|---:|
| x86-64 | 12 | +0.194974 % | +0.066731 % |
| arm64 | 12 | +0.113848 % | +0.062119 % |
| wasm32 | 10 | +0.248758 % | +0.057039 % |
| wasm64 | 11 | +0.233999 % | +0.057260 % |

Wasm32 lacks `atomic_ops` and `bitint_runtime`, wasm64 lacks
`bitint_runtime`; the same cases are rejected by all three compilers. The
report contains commands, hash values, support outcomes and instruction
counts in `work/opt-profile/stack-cost/final/result.json`; the run log is in
`work/opt-profile/stack-cost/final-run.log`. This is a bounded gate for
compiler work, not a measure of the generated programs' run speed.

After integration of the postfix parent (`e92a939`) unity and nonunity each
pass all 16 target×O0-O3 runtime/codegen cases, plus four `VCC_NOOPT` cases
each. The checks cover pure expressions, calls once, short circuit,
volatile/atomic destination/read, division/memory read/FP conversion,
cleanup/VLA/brace-init, `used`/`optnone`/asm register and reused inlined
AST. The exact `f7327bd` baseline tool fails the new x64 positive probe, so
the codegen requirement separates actual new capability from an already
approved outcome. ABI cross calls pass 32/32 on x64 and 32/32 on a64. The
debug probe has its own unused initializer before a surviving line marker
and passes O0-O3, including DWARF verification and byte-identical `.text`
with/without `-g`.

The native regression check on the same production source gave 230 passed
x86 cases and 235 arm64 cases, plus one existing arm64 XFAIL. The new
fixture was first rejected by clang: GNU's `__atomic_fetch_add` took an
`_Atomic` pointer. The fixture was corrected to typed
`__c11_atomic_fetch_add`, which clang accepts; the affected case was then
run separately and passes on both native targets. In total 231 x86 cases
(six reference exceptions) and 236 arm64 cases plus one existing XFAIL are
thus verified. No production code was changed by the correction. All four
targets' O0-O3 run/code requirements and O2/noopt probes were rerun and pass
with the corrected fixture. Logs: `work/opt-pr-tmp/unused-stack-
{x86,a64}.log`; the two new separate 1/1 results are runs of the affected
fixture, not new full suite runs. The combined stack's cost gate remains.

### Shared walker: one value read for scalar postfix (2026-11-11)

Status: implemented and locally verified in `opt/postfix-eval`, before the
integration commit. `wk_incdec` in `codegen/walk/expr.c` reads an ordinary
register-represented value once, keeps the old value and makes a register copy
in the right register class before the destructive arithmetic. This is an
evaluation fix at O0–O3, with no optimization pass, SSA or costly analysis.
The shared walker is still used only by x86; arm64 and wasm keep their existing
paths and are validated by the same semantic test.

Scope limit: `_Atomic` goes unchanged through its separate RMW/CAS path. Types
that `wk_mem` holds by address, for example x86 `__int128` and `long double`,
also keep their existing snapshot/read path; this sub-step does not promise a
single original read for them. Postfix bit-fields use the new register copy:
two container reads remain, one for the field value and one to preserve neighbouring bits,
as in the clang/GCC cases examined. The prefix bit-fields' extra result read and
field-width normalization are not changed here.

Validation: `postfix_eval.c` and `vcc-opt-rules.py --rule postfix --run`
check exactly one ordinary scalar read and write (or one combined
RMW), old/new values, discarded and cast results, narrow integers, bool,
FP/GPR register pressure, fixed address registers, pointer steps, side effect in the address,
bit-field neighbours and separate atomic/address-based checks. The unity and
file-by-file builds pass all 32 combinations of four targets and O0–O3. The earlier
unchanged parent69 postfix path is rejected by the read check: two reads
instead of one. x86's own C suite passes 228/228, with six existing skipped.
Logs: `build/linux/postfix-evidence/`. No further broad test run is needed
for this sub-step. A small matched Ir comparison after checkpoint 6a726c0 is in
`build/linux/postfix-evidence/cost/result.json`: the same clang23,
`-O3 -march=x86-64-v3`, exact f7327bd baseline and one serial Callgrind run
per binary/source, including process start. For 64 functions without postfix,
6 703 455 changes to 6 703 542 Ir (+87; +0.0013 %), with identical assembler text.
For 64 postfix functions, 6 717 886 changes to 6 731 413 Ir (+13 527;
+0.2014 %), while each function's ordinary object reads decrease
from two to one. This measures compiler work in two small samples, not a full
1 % gate or run time/speed gain of the generated program.


OPT1 integration after the wasm-flow parent: on `60027c7` with parent
`6d60e73`, unity and file-by-file (j2) pass, 16 complete byte-identical
assembler pairs between the build modes, all 16 multiplication cases O0–O3 on
x86/arm64/wasm32/wasm64, a sanitized recipe with mutation and a post-effect contract.
36 O0/VCC_NOOPT/overflow comparisons against the parent's binary are byte-identical
(built 1458a90, production sources verified identical to 6d60e73).
The native run harness now also checks preserved registers for this case.
The CI/harness union keeps tails, deadresults, flow contracts, wasm-flow and
mulconst. The current unity binary has SHA256
`398f139ca1489cd83a8822c0db8958f54df72cc73cc7fb252e5add4781a30a2e`;
commands/binary hashes and 16 pairs are in
`work/opt-profile/mulconst-integration-parity/result.json`, run/contract logs
in `work/mulconst-integration-{contracts,policy,runtime}.log`.
No historical cost profile has been repeated: the figures above still apply to
exactly 69c1bd7 to f92b67b, not this integration parent. Integration CI
remains; G5/G7 and all of O2 are still open.

Postfix integration after the multiplication parent: e63bf47 stacks the fix
on PR71/f295999. The current unity build is green. All 16 postfix and 16
constant-multiplication cases (four targets, O0–O3) pass code generation checks and
run; the multiplication recipe with mutation check and register effects passes
ASan/UBSan. `build/linux/postfix-evidence/integration-*.log` contains the results.
The earlier broad suite and the matched Ir samples have not been rerun; no
integration failure motivated it. The fix is still limited to
register-represented postfix and stays on top of the multiplication parent.

### Emitter: name and register comparisons (2026-11-11)

On base `12957dc`, the already stored words of interned identifiers are used for short
names (`codegen/name.h`), integer tags from the existing builtin table for
specially treated library calls, and direct decoding of x86's asm register names.
The unused parallel register table lookup has been removed. The number of
source lines with `strcmp`/`strncmp`/`memcmp` in the emitter and three backends decreases
from 60 to 33; what remains includes actual C string comparisons,
variable asm operand names, section/visibility names and debug files. These are
not removed merely by moving or recreating a comparison loop.

Matched `perf stat`, five alternating runs on this shared machine with clang 23
`-O3 -march=x86-64-v3`, `12957dc` → `2727265`: 1 000 x86 functions with
inline asm clobbers gave −7.586 % compiler instructions, and 1 000 ordinary
calls and `__builtin_memcpy` respectively gave x86 −0.235 / −0.146 %, arm64
−0.202 / −0.103 %. The output assembler was identical. On wasm the same
call samples increased +0.033–0.035 %; the control samples −0.027 to +0.012 % on four targets.
These saturated samples do not measure general compile time. The five short
cycle measurements vary and prove no 1 % time bound. Commands,
binary/source hashes and observations: `work/opt-profile/emitname/final-result.json`.

The fixed 1 % instruction gate against the same base passes: x86 −0.002 %, arm64
+0.020 %, wasm32 +0.042 %, wasm64 +0.039 %; 12/12 native files, 10 wasm32 and
11 wasm64, with rejected wasm cases explicitly reported. Basis and exact
build provenance: `work/opt-profile/emitname-final-gate/result.json`. The unity and
file-by-file builds pass; before/after kernel sema 887/887 in batch and individually;
248/248 arm64-vcc regressions, one existing xfail; 90 assembler pairs O0/O2,
187 x86 clobber spellings, four targets' O0–O3 control flow and ASan/UBSan
contracts. Further review of the remaining comparisons and realistic
wall/CPU time measurement remain.

### Bounded switch value tables (2026-11-11)

Status: locally implemented for x86-64, arm64 and wasm32/64 at O1–O3.
A shared budgeted analysis accepts dense ranges of at most 256 elements
with an explicit default and constant integer results, either direct
returns or assignment to the same local variable followed by break. Holes get
the default value and the selector is evaluated once. Cleanup, inlining on native,
Darwin and unsafe control flow keep the ordinary switch lowering. Data is placed
after the function's size and the debug/CFI end in an aligned readable section.

`vcc-opt-rules.py --rule switchvalues --run` passes O0–O3 on all four targets
and O2 with `VCC_NOOPT`; the test covers boundary values, ranges, holes, side effects,
volatile, multiple tables and rejected control flow. Arm64 direct objects with
PIC, debug and CFI are also run under qemu and their sections, symbols and
relocations agree with external assembly of vcc's assembler.
The cost samples below do not give general compile time or program speed.
### OPT1: two bit rules (2026-11-11)

Multiplication by a power of two is lowered directly to a machine shift for 32/64-bit
integers; XOR with all ones becomes complement. The recipes are in separate
target-generic files, with no new AST walk. O0 and VCC_NOOPT are unchanged;
_BitInt and wider types are not included. O1–O3, side effects and operand conversion
are checked on x86, arm64, wasm32 and wasm64. Unity and file-by-file builds and
36 O0/noopt/overflow parity samples passed.

The matched serial Callgrind profile (`work/opt-profile/bit-rule-audit/result.json`,
compiler sources and binary hashes in the manifest) measures the instructions of the whole compiler
process, not cycles or program run time. Four mixed x86 samples gave
+0.0112 %; 200 rule-free functions +0.052–0.469 % depending on target; 200
rule-saturated functions −1.39–1.79 %. These are small samples, not a full
performance gate. Comparison rules for integer bounds and broad O2 coverage remain.

### OPT1: integer comparisons at boundary values (2026-11-11)

A shared recipe in `codegen/opt/cmpbounds.c` recognizes relations with a
constant result at the lowest/highest value for converted 32/64-bit
integers. The operand is still evaluated: calls and volatile/atomic reads
are preserved. Only numeric AST literals with an identical converted type are included;
pointers, floating point, _BitInt, wider types and other constant expressions fall
through. Value and branch are tested on x86, arm64, wasm32 and wasm64 O0–O3;
assembler checks also verify calls and volatile/atomic reads.
O0 and VCC_NOOPT are byte-identical to the parent on the test file for all four targets.
Unity and file-by-file builds pass. Further boundary rules remain.

The matched serial Callgrind profile against the bitset parent (`work/opt-profile/
cmpbounds-cost/result.json`, with source and binary hashes) measures instructions for the
whole compiler process. Four mixed x86 files gave +0.181 %; 200 functions
without boundary comparison gave +0.252–0.295 % and saturated positive samples −1.35–8.53 %
depending on target. These are small samples, not a full performance gate.

The separate matched Callgrind samples of the value tables against the base before these later
steps are in `work/opt-profile/switch-values-cost/result.json`. A sample with
200 functions without switch changed the compiler work +0.003–0.023 %; a
deliberately saturated sample with 200 value-table switches increased it +6.16–6.36 %
on native and +20.05–20.16 % on wasm. These are compiler instructions, not
cycles, and require a realistic corpus before a general conclusion. A separate
synthetic x86 program with 100 000 calls decreased the instructions of the whole
program from 4 544 608 to 4 217 359 (−7.2 %); it is not a general
run time measure. The composite stack is measured separately below.

The whole stack including value tables passes the fixed 1 % gate against
`4a839ba`: x86 +0.375 %, arm64 +0.292 %, wasm32 +0.556 %, wasm64 +0.515 %
compiler instructions. The value table step alone against the comparison parent
`dc3e54d` gave +0.032 %, +0.023 %, +0.045 % and +0.043 % respectively. That is
12/12 native files, 10 wasm32 and 11 wasm64; rejected atomic_ops and/or
bitint_runtime are reported, not approved. Exact source tree and binary provenance,
commands and error checking are in `work/opt-profile/full-stack-cost/result.json`.
The gate is a small fixed corpus and does not overrule the higher saturated switch sample.

CI follow-up: the assembler text of the wasm tables now gets an explicit `.size`,
which LLVM's assembler requires. The focused table sample is also assembled with
clang for wasm32/64; a separate switch sample with variable results preserves
the check of wasm `br_table`. Both samples run O0–O3 on four targets. The
previous cost profile applies before this small metadata addition.

### Switch value tables: faster output (2026-11-11)

`perf record` and Callgrind on a switch-measured wasm64 sample pointed to
`vsnprintf` via `tb_f` for each table byte. The byte output now uses
`tb_s`/`tb_u`/`tb_c` instead and produces byte-identical assembler. Matched
Clang 23 `-O3 -march=x86-64-v3`, `096ead5` → `0a06d20`: five alternating
`perf stat` runs per binary on 2 000 switch functions gave medians for the
compiler process's user instructions / user cycles: x86 −17.91 / −13.28 %,
arm64 −17.02 / −12.85 %, wasm32 −18.67 / −14.13 %, wasm64 −18.65 / −13.64 %.
Rule-free x86/wasm64 controls changed instructions −0.0013 / −0.0017 %;
cycle figures on this shared machine vary and are not a general speed gain.
A separate Callgrind measurement with 200 switch functions gave −12.1–13.1 % Ir.
Commands, binary/input hashes and all measured values are in
`work/opt-profile/switch-speed/{perf-stat.json,run.log}`. O0–O3 runs and
optimization checks pass on four targets; wasm is also assembled by clang.
This measures compilation, not the run time of the generated code.

### Integration on main: arithmetic and switch (2026-11-11)

The earlier PR #77–#82 were merged into feature branches after their parents
had already been merged. Integration commit `d24fb15` brings the whole stack into main
`ccbbc21`: bit rules, boundary comparisons, switch bitset, switch value tables,
wasm `.size` and faster table output. Conflicts in the plans were resolved by
keeping both sides' history; the emitter and CI changes from main are
kept.

Unity and file-by-file builds pass. In both build modes, bit rules,
boundary comparisons, switch tables, bitset and constant checks run O0–O3 on x86,
arm64, wasm32 and wasm64; relevant noopt cases, external wasm assembly and
arm64 direct objects with PIC/debug/CFI are included. The emitter name contracts pass
ASan/UBSan. The vcc suite: arm64 252/252 and one existing xfail; x86 247/247
runnable and six reference exceptions.

The matched serial Callgrind gate for the whole integration against `ccbbc21`, both
binaries built with clang 23 `-O3 -march=x86-64-v3`, gave compiler Ir: x86
+0.169 %, arm64 +0.143 %, wasm32 +0.274 %, wasm64 +0.248 %. 12/12 native files,
10 wasm32 and 11 wasm64 are included; atomic_ops/bitint_runtime, which are rejected by both
compilers on the respective wasm targets, are reported separately. The 1 % gate
passes. Source tree/binary hashes, exact commands and cases are in
`work/opt-profile/consolidated-main-cost/result.json`. This is a small fixed
corpus of compiler instructions, not a bound on wall or CPU time.

### Type bounds written as constant expressions (2026-11-11)

`codegen/opt/cmpbounds.c` now also recognizes pure, bounded integer expressions
as type bounds, for example `x < (-2147483647-1)` and `x <= (unsigned)-1`.
An expression of at most 16 nodes is checked before the existing
constant evaluator; calls, variable reads and unknown expressions fall back
to ordinary code generation. The second operand's side effects are preserved.
Focused run and code generation tests pass O0–O3 on x86, arm64,
wasm32 and wasm64 with both unity and file-by-file builds. O0 and `VCC_NOOPT=1`
gave byte-identical assembler against the base on all four targets. The vcc suite passes
252/252 with one existing xfail, and the x86 regression passes.

The matched serial Callgrind profile against `90b5cf8` gave changes in
compiler instructions of −0.017 % for x86, −0.014 % for arm64, −0.031 %
for wasm32 and −0.026 % for wasm64. The fixed 1 % gate passes; 12/12
native files, 10 wasm32 and 11 wasm64 are included, while atomic_ops/bitint_runtime,
which are rejected by both on the respective wasm targets, are reported separately. Provenance,
commands and cases are in `work/opt-profile/bounds-expressions-cost/result.json`.
This is a small corpus and instruction counts, not a measure of CPU time.
General O2 coverage and G5/G7 remain.

### Identical integer operands (2026-11-11)

A shared recipe in `codegen/opt/repeated.c` simplifies `x-x` and `x^x` to
zero and `x&x`/`x|x` to one read for the same ordinary automatic
integer variable. Address-taken, volatile, atomic, static and external values
are rejected; no arbitrary expressions are compared. All three emitter families use
the decision. Focused O0–O3 samples pass on x86, arm64, wasm32 and wasm64
with both unity and file-by-file builds; two volatile reads are preserved. O0 and
`VCC_NOOPT=1` give byte-identical assembler against the base. The vcc suite passes
253/253 with one existing xfail.

The matched serial Callgrind gate against `22a91b8` gave changes in
compiler instructions +0.099 % x86, +0.121 % arm64, +0.115 % wasm32 and
+0.123 % wasm64; the 1 % gate passes. 12 native files, 10 wasm32 and 11
wasm64 are included; wasm cases that both reject and the measurement provenance are in
`work/opt-profile/repeated-integer-cost/result.json`. It is a small corpus
with instruction counts, not CPU time; general O2 coverage and G5/G7 remain.

### Wasm: integer comparison with zero (2026-11-11)

A shared, bounded recipe in `codegen/opt/cmpzero.c` recognizes
integer equality with zero and unsigned `x <= 0` (also with reversed operands).
Wasm lowers these directly to `i32.eqz`/`i64.eqz` instead of a constant
and a comparison. Constant expressions are checked with the existing bounded
constant evaluator; the second operand is evaluated once. Signed relation,
dynamic second operand and side effects are covered by the regression tests.
Both build modes' focused O0–O3 samples pass on four targets; O0 and
`VCC_NOOPT=1` give byte-identical assembler against the base. The vcc suite passes
253/253 with one existing xfail. The recipe is used only where an individual
target command gives a concrete effect; the native lowering is unchanged.

The matched serial Callgrind gate against `22a91b8` gave changes in
compiler instructions −0.028 % x86, −0.024 % arm64, +0.137 % wasm32 and
+0.139 % wasm64; the 1 % gate passes. 12 native files, 10 wasm32 and 11
wasm64 are included; the atomic_ops/bitint_runtime cases that both reject on the respective
wasm targets are reported in `work/opt-profile/zero-comparisons-cost/result.json`.
It is a small corpus and instruction counts, not CPU time. G5/G7 and general
O2 coverage remain open.

The same recipe now also covers integer comparisons between two reads of the
same ordinary local variable, also directly in x86's condition lowering.
Floating-point comparisons are kept because of NaN. Extended focused O0–O3 samples
pass on four targets in both build modes; the vcc suite 254/254 with one existing
xfail and O0/noopt assembler identical to the stacked parent. Matched
Callgrind against PR #89's `16d5feb` for the whole extended rule: x86 +0.133 %,
arm64 +0.123 %, wasm32 +0.139 %, wasm64 +0.109 % compiler instructions;
the 1 % gate passes on the same bounded corpus. Exact cases and provenance:
`work/opt-profile/repeated-expanded-cost/result.json`. This does not measure CPU time.

Supplementary short CPU time profile against PR #89's binary: five alternating
`perf stat task-clock:u` runs per binary on synthetic files with 12 000
functions. Median base → candidate, x86: identity-saturated 25.21 → 23.18 ms,
control without hit 28.80 → 28.21 ms; wasm64: saturated 22.83 → 22.32 ms,
control 25.19 → 25.87 ms. The controls' assembler is identical. The short samples
of the small, shared host vary and do not show that the CPU time overhead on
real projects is under 1 %. Measurement commands, binary/input hashes and all
samples are in `work/opt-profile/repeated-cputime/{measure.py,result.json}`.

Supplementary real input: two preprocessed Linux TUs among the first
30 available were accepted by the standalone `-O2 -ffreestanding` probe.
Five alternating `task-clock:u` samples per binary gave median base → candidate
23.03 → 23.02 ms for `kernel__printk__sysctl.o.i` and 15.89 → 15.59 ms for
`fs__fs_dirent.o.i`; the assembler was identical in both cases. The other 28
failed in this simplified run and are not included: this selective,
small sample does not prove the compiler's cost on a whole build. Commands,
hashes and samples are in `work/opt-profile/repeated-cputime/kernel*.{py,json}`.

Broader CPU time samples for the same binaries: after restoring the relevant
flags from the kernel's `.cmd`, 28 of the first 30 preprocessed
Linux TUs were accepted by both. Three alternating serial rounds of all 28 gave
median user CPU 0.78945 → 0.79150 s (+0.26 %); system CPU and wall time
are also in the raw data. Two TUs were excluded after the candidate's preflight.
The sample is small, the host is shared and the measurement concerns the arm64 target, not a
whole kernel build or wasm. Exact flags, source/binary hashes and samples are in
`work/opt-profile/repeated-cputime/kernel-batch{.py,-result.json}`.

### Double unary integer operations (2026-11-11)

`codegen/opt/double-unary.c` identifies `-(-x)` and `~(~x)` for same-typed
32/64-bit integers; each emitter then evaluates `x` once. Floating point,
BitInt and intermediate type conversions are left to ordinary lowering.
Focused run/code generation samples O0–O3 pass on four targets in both
build modes, including calls, volatile and narrow conversion. O0/noopt gives
byte-identical assembler against the stacked parent; the vcc suite passes
255/255 with one existing xfail.

Matched Callgrind against `e1391de` gave compiler Ir +0.045 % x86, +0.049 %
arm64, +0.106 % wasm32 and +0.119 % wasm64 on the fixed small corpus;
the 1 % gate passes, with the same explicitly reported wasm exclusions.
Three alternating serial rounds on 28 preprocessed Linux TUs with arm64 flags
gave median user CPU 0.79154 → 0.78655 s. Two TUs are excluded and
the shared host's small sample proves no general time gain. Provenance,
flags and samples: `work/opt-profile/double-unary-cost/result.json` and
`work/opt-profile/double-unary-cputime/kernel-batch-result.json`. G5/G7 and
general O2 coverage remain.

### Next step: bounded local reuse of values (2026-11-11)

A concrete gap is `(a+b)*(a+b)`: current x86, arm64 and wasm emit
two additions, while Clang 23 `-O2` computes the sum once. Even when the
sum is assigned to a local variable, the corresponding expression is recomputed. This
is local CSE, not solved by the identity rules above.

The first check basis is `test/vcc/local_value_reuse.c`, which runs O0–O3
on four targets via `--rule localvalues`: ordinary reuse and commutation,
changed operand, call that changes global data, volatile, possible pointer alias and
control flow join. These are only semantic regressions; they do not prove
that any optimization is implemented. The base passes all 16 combinations.

Carry out in small reviewable steps: (1) describe pure integer expressions with type,
operation and stable value identities in a bounded table per basic block;
require a budget and a cheap candidate check so ordinary code avoids allocation and
full extra walks; (2) invalidate at definitions, unknown effects,
control flow boundaries and aliasing memory; volatile/atomic, trapping and
floating-point-environment-dependent operations must never be reused without separate proof;
(3) give the x86/arm64 adapters correct register lifetime, implicit register/flag effect
and debug remapping, and wasm a typed local/stack handling rather than
assembler text rewriting; (4) add code shape requirements only when the respective
backend is implemented, along with budget/barrier/alias/join negative cases and
O0/noopt parity. Measure actual compile CPU and instructions for both
controls and expression-saturated files as well as the composite stack. None of the
points is implemented or approved yet; all of O2 and G5/G7 remain.

The first building block exists locally in `codegen/flow/values.{h,c}`: a fixed 32-slot
table without allocation with explicit budget steps. A collision can only discard
opportunities; at budget end no more facts are published before reset. The adapter
must itself create new value IDs at definition, reset at a barrier and
prove that a result still exists in a usable place. ASan/UBSan
contracts test different key parts, collision, block boundary and budget end;
the unity and file-by-file builds and the 16 semantic target/level samples pass.
No backend uses the table yet; it thus optimizes no code and is
not a completed CSE solution. The next work is an actual consumer with safe
place handling and a documented candidate/cost bound.

The first consumer now exists in `codegen/opt/sibling-values.c`: when both
operands of an integer operation are pure, structurally identical expressions
of ordinary local values, the expression is computed once. The limit is 32
AST nodes and 64 table operations per candidate; a cheap root check precedes
the allocation of the small table on the stack. Collision and budget end give a
conservative fallback. Commutative inner operations may have reversed operands.
Floating point, BitInt, indirect reads, volatile, atomic, address-taken locals,
calls and unproven operations are rejected. Native keeps the value in separate
registers; wasm uses a typed temporary local. This is only reuse
between siblings in an expression, not general CSE across statements or blocks.

O0–O3 run/code generation samples pass on x86, arm64, wasm32 and wasm64
in both build modes, including nested expressions, alias, volatile, atomic, calls
and joins. The ASan/UBSan table contract passes; O0/noopt assembler is identical
against `b261315`. The vcc suite passes 256/256 with one existing xfail. The matched
Callgrind corpus against the same base gave compiler Ir +0.085 % x86, +0.088 % arm64,
+0.100 % wasm32, +0.087 % wasm64; the 1 % gate passes. Three alternating
serial rounds of 28 preprocessed arm64 kernel TUs gave median user CPU
0.79110 → 0.79335 s (+0.28 %). Two TUs are excluded and the limited
sample on a shared host does not fully prove the cost of a whole build or wasm. Provenance
is in `work/opt-profile/local-values-cost/result.json` and
`work/opt-profile/local-values-cputime/kernel-batch-result.json`. Reuse across
statements/blocks, general CSE and G5/G7 remain.

After #93 was merged to main and arm64's ABI changes were added, both
sides were rebuilt against exactly main `87975fa`. The four-target samples pass in both
build modes and arm64's cross-compiler ABI passes 34/34. The matched fixed
Callgrind gate now gives compiler Ir +0.112 % x86, +0.113 % arm64, +0.119 %
wasm32 and +0.117 % wasm64; the 1 % gate passes with the same reported
wasm exclusions. This measurement does not replace CPU time and the basis is in
`work/opt-profile/local-values-main-cost/result.json`.

### Local initialization followed by a pure return (2026-11-11)

The bounded value numbering recipe now also reuses an ordinary local
integer value in a directly following pure return: `unsigned x=a+b; return
x*(a+b);` computes the sum once on x86, arm64 and wasm. The return node must
already read `x`, so its initialization cannot disappear as unused. The same
32-node/64-table budget and pure integer requirements are used; another statement, write,
call, indirect memory, volatile, atomic, address-taken variables and unproven
expressions abstain. The substitution applies only under that return statement and is saved/
restored per emitter context. This is still not general block-local CSE.

O0–O3 run/code generation samples pass on four targets in both build modes,
including write and volatile local as negative cases. O0/noopt assembler
is identical against `597c521`; the vcc suite passes 256/256 with one existing xfail.
Matched Callgrind against that parent gave compiler Ir +0.102 % x86, +0.088 %
arm64, −0.003 % wasm32 and +0.004 % wasm64 on the fixed small corpus; the 1 %
gate passes. Three alternating serial rounds on 28 preprocessed arm64
kernel TUs gave median user CPU 0.79289 → 0.78492 s; two TUs are
excluded and the small sample on a shared host proves no general time gain.
Details and hashed provenance: `work/opt-profile/decl-return-cost/result.json`
and `work/opt-profile/decl-return-cputime/kernel-batch-result.json`.
General local CSE, G5/G7 and full O2 coverage remain.
The same 28-TU sample at O0 gave median user CPU 0.76171 → 0.76136 s over
three alternating rounds. This short sample on a shared host is not a general
CPU time gate; raw data is in `work/opt-profile/decl-return-o0-cputime/`.

Extension: the return rule now searches backwards over at most eight pure local
integer declarations in the same block. Writes, calls, volatile/atomic,
cleanup, control flow and unproven initializers stop the search. A fixed
candidate's key is preserved during the search even if the direct-mapped table
gets a collision; budget end still abstains. Focused code shape and
run samples cover a pure intermediate declaration as well as write, call
and volatile barriers on all four targets O0–O3 and both build modes. O0/noopt is
byte-identical to the parent; the vcc suite 256/256 with one existing xfail and
x86 debug lines O0–O3 pass.

New matched Callgrind profile against `597c521`: compiler Ir +0.114 % x86, +0.094 %
arm64, +0.010 % wasm32, +0.015 % wasm64; the 1 % gate passes on the fixed
bounded corpus with previously reported wasm exclusions. Seven alternating
rounds on 28 preprocessed arm64 kernel TUs gave median user CPU O2
0.78091 → 0.78181 s and O0 0.76889 → 0.76712 s. An earlier three-sample test
varied more; these small, selective samples on a shared host do not give a general
CPU time bound. Raw data and hashed provenance are in
`work/opt-profile/decl-return-expanded-cost/`, `decl-return-expanded-cputime/`,
`decl-return-expanded-cputime7/` and `decl-return-expanded-o0-7/`. Search over
other statements, join points and general CSE remain.

The same bounded block scan now reuses values also in a later pure
local integer initialization, not only in return. The receiver must already read the
earlier local; write, call, volatile/atomic and other barriers
are rejected as before. New run and code shape requirements pass O0–O3 on four
targets in both build modes, including a writing initializer as a negative case.
O0/noopt assembler is byte-identical to `597c521`; the vcc suite is 256/256 with
one existing xfail and x86's debug samples pass.

The updated fixed Callgrind corpus against the same base gives compiler Ir
+0.151 % x86, +0.130 % arm64, +0.036 % wasm32 and +0.031 % wasm64; the 1 %
gate passes with previously reported exclusions. Seven alternating O2
rounds on 28 preprocessed arm64 kernel TUs gave median user CPU 0.78387 →
0.78438 s; three O0 rounds gave 0.76742 → 0.75814 s. Two TUs were excluded and
short samples on a shared host give no general time bound. Provenance and samples:
`work/opt-profile/local-values-decl-cost/`, `local-values-decl-cputime/` and
`local-values-decl-o0/`. General block-local CSE and reuse across joins remain.

The bounded block scan can now pass ordinary local assignments,
increments and simple compound assignments. Written symbols are collected;
the candidate is rejected if the cached local or any input has been written.
Pointers, volatile/atomic, calls, hidden writes, division/shift and other
unproven effects are barriers. The purity check of an assignment's right-hand side
is deferred until a candidate has survived the invalidation check; simple written
inputs are rejected before the table is created. The tests cover independent write,
written input, written cached value, compound assignment, increment and
hidden write/call/volatile; four targets O0–O3 and both build modes pass.
O0/noopt gives byte-identical assembler against `597c521`; vcc 256/256 with one
existing xfail, x86 debug and ASan/UBSan table contracts pass.

Matched Callgrind against the same parent: compiler Ir +0.156 % x86, +0.147 %
arm64, +0.044 % wasm32 and +0.046 % wasm64; the 1 % gate passes with the same
reported exclusions. Seven alternating O2 rounds on 28 preprocessed
arm64 kernel TUs gave median user CPU 0.78569 → 0.78586 s; two TUs are
excluded and the short sample on a shared host shows no general time bound.
A deliberately saturated control sample with 2 000 invalidated candidates gave
+2.32 % compiler Ir on x86 and +2.52 % on wasm64, with identical assembler.
Before the deferred check and the fast rejection, the corresponding figures were +3.77 %
and +4.26 %. It is a real bounded cost case, not the whole project's
cost. Exact commands, binary/source hashes and samples are in
`work/opt-profile/local-values-writes-fast-cost/`,
`local-values-writes-fast-cputime/`, `local-values-writes-saturated/` and
`local-values-writes-fast-saturated/`. General CSE and joins remain.

### Local value reuse in assignments (2026-11-11)

The budgeted block scan can now reuse an earlier local in the
right-hand side of an ordinary local integer assignment, also selected compound
assignments. The target can be the cached local or one of its inputs:
the substitution applies only to the pure right-hand side before the write. Earlier
writes invalidate as before; indirect, volatile and atomic targets and
right-hand sides with side effects are rejected. Pure integer conversions can now be part of
the bounded value numbering proof; floating-point conversions remain
barriers. This is still not general CSE or reuse across a
control flow join.

Four-target samples O0–O3 in both build modes cover ordinary and compound targets,
write to cached value/input before and during consumption respectively,
volatile and hidden write, integer/floating-point conversion and code shape.
O0/noopt is byte-identical to `143e9c0`; the vcc suite passes 256/256 with one
existing xfail, x86 debug and ASan/UBSan table contracts pass. The matched
Callgrind corpus against that parent gave compiler Ir +0.057 % x86, +0.046 %
arm64, +0.074 % wasm32 and +0.060 % wasm64; the 1 % gate passes with previously
reported wasm exclusions. Seven alternating O2 rounds with 28 preprocessed
arm64 kernel TUs gave median user CPU 0.78612 → 0.79193 s; two TUs are
excluded and short samples on a shared host show no general time bound.
A deliberately saturated control with 2 000 invalidated candidates gave
+1.83 % compiler Ir x86 and +2.22 % wasm64, with identical assembler. Results,
commands and hashed provenance are in `work/opt-profile/local-values-assign-
{cost,cputime,saturated}/`. G5/G7 and general O2 coverage remain.

### Integration of local value reuse

PR #97 and #100 had been merged into already integrated feature branches. They are
therefore integrated on top of main `94c93d3a` (including switch jump tables) in
merge `345796ab`. The localvalues and switch samples pass O0–O3 on x86, arm64,
wasm32 and wasm64 in the unity and separate-file builds; O0 and O2 with VCC_NOOPT are
byte-identical to main. The vcc suite passes 258/258 with one existing xfail;
the ASan/UBSan sample for the value table passes.

The matched Callgrind corpus against main gives compiler Ir +0.204 % x86, +0.174 %
arm64, +0.112 % wasm32 and +0.104 % wasm64, under the 1 % budget. The corpus has
12 files for the native targets, 10 for wasm32 and 11 for wasm64; atomics and
BitInt cases not supported by the respective targets were excluded on both sides. Seven
alternating rounds with 28 preprocessed arm64 kernel files gave median user CPU
0.825 → 0.791 s; two files were excluded after preflight and short samples on a shared
host show no general speed change. Provenance is in
`work/opt-profile/local-values-integration-{cost,cputime}/`.
