# Fast kernel compilation – Phase 1: Measurement and foundation

*Working name: **ccs** (compile server). Written 2026-11-07.*

## Background

Long-term goal: a compiler server that builds the Linux kernel at the `-O0` level
10× faster than `gcc`/`clang`. `/usr/bin/cc` (or `make CC=...`) becomes a thin
client that sends argv/cwd/fds over a Unix socket to a long-lived server.

The hypothesis behind the whole project:

1. The frontend (preprocessor + parser + Sema) accounts for most of the time per
   translation unit, and almost all of that work is header work that is repeated.
2. With a cache keyed on *(header, macros read, declarations looked up)*
   almost all header work becomes cache hits.
3. A single-pass backend (AST → machine code via a value stack, à la TCC/TPDE) takes
   care of the rest.

**Phase 1 does not build a compiler.** It produces the numbers that decide whether
the hypothesis holds, and builds the infrastructure (environment, measurement,
client/server) that all later phases stand on.

## Goals for Phase 1

| # | Question / deliverable | Answer in the form of |
|---|------------------------|-----------------------|
| M1 | Where does the time go in a kernel build today? | Table: total, per Kbuild step, frontend vs backend |
| M2 | How much header work is repeated? | Share of tokens/declarations that would be cache hits |
| M3 | How much do process/make overhead cost? | Measurement with proxy server vs direct compiler |
| M4 | Working thin client + server | Kernel built via proxy, byte-identical, boots in QEMU |
| M5 | Go/no-go for Phase 2 | Short report with the decision and the choice of frontend base |

## Scope limits

- Architecture: **x86-64** only.
- Kernel: **v7.4-rc1**, locked commit. (It already contains the first half of
  Stoakes's kbuild-speedup series; v5 of the rest can be applied for comparison.)
- Configurations:
  - `tinyconfig` – fast iteration
  - `defconfig` + fragment `env/fas1.config` – main measurement
- Fragment `fas1.config`: `DEBUG_INFO=n`, `DEBUG_INFO_BTF=n`, no LTO,
  no KASAN/KCSAN/KCFI, `MODULES=n` (everything `=y`), `KERNEL_LZ4=y`
  (or uncompressed).
- Compilers: current GCC and Clang from the distribution.
- Outside Phase 1: own preprocessor/parser/backend, in-memory linking,
  incremental mode, other architectures.

## Steps

### 1.1 Environment

**Changed 2026-11-07:** we build directly on macOS (Apple Silicon, M1 Pro),
without a container. The target architecture in Phase 1 therefore becomes **arm64** (`ARCH=arm64
LLVM=1`, Homebrew LLVM); x86-64 as cross-compilation later (requires objtool and
more host work). Linux-specific things (io_uring etc.) are ported later.

- `env/setup-macos.sh`: Homebrew tools, case-sensitive sparsebundle
  `work.sparsebundle` mounted on `work/`, cloning of the kernel, configurations.
- `env/macos-env.sh`: GNU tools first in `PATH` and a `kmake` function.
- `env/host-include/`: what macOS lacks for the kernel's host tools:
  `elf.h` (musl, MIT), `byteswap.h`/`endian.h` (own shims) and
  `vroom-prelude.h` (prevents Darwin's `uuid_t`, which collides with
  `scripts/mod/file2alias.c`). No change in the kernel tree is needed.
- No `gcc` as a target (only clang/LLVM) in Phase 1 on macOS.
- Reproducibility: set `KBUILD_BUILD_TIMESTAMP`, `KBUILD_BUILD_USER`,
  `KBUILD_BUILD_HOST`, `SOURCE_DATE_EPOCH` so that two builds can be compared
  byte for byte.
- Log CPU model, number of cores, RAM, compiler versions in every result.

**Done when:** `kmake defconfig && kmake -j$(sysctl -n hw.ncpu) Image` passes
on macOS and two builds give an identical `vmlinux`.

### 1.2 Baseline (M1)

`bench/baseline.sh` runs, for gcc and clang, best-of-N (N=3–5) with a warm page cache:

- clean build, no-op build, incremental build (touch of a `.c` file and of a
  commonly included header, e.g. `include/linux/sched.h`).

Breakdown:

- **Per Kbuild step:** a wrapper around `CC`, `LD`, `AS` and script steps that logs
  start/stop + argv to a file (the same mechanism as the client in 1.4 –
  write it once). Aggregate: compilation, `.S`, linking, kallsyms,
  objtool, modpost, compression, other (make/sh).
- **Frontend vs backend:** `clang -O2 -ftime-trace` (the kernel's real flags)
  on all TUs, aggregate "Frontend" / "Backend" / "CodeGen" from JSON.
  Complement with `-fsyntax-only` as a quick check of the frontend share.
- **-O0 proxy:** the same with a variant that forces `-O0` on the files that
  can take it (the build does not need to link) – gives a picture of the backend cost
  at -O0 compared to -O2.
- **Critical path:** timeline from the wrapper log (Chrome trace format) that
  shows serial phases and core utilization.

**Done when:** `results/baseline.md` with tables and a timeline.

### 1.3 Analysis of header redundancy (M2)

The most important metric of the phase. Two levels:

**a) Upper bound (simple):** `-E` with line markers on all TUs. Count tokens per
source file (header) and how many times the same header is processed in total.
→ "X % of all tokens come from headers that have already been processed in another TU."

**b) Exact cache key (real):** A Clang plugin (`analysis/ppdeps/`) with
`PPCallbacks` + Sema hooks that records, per header inclusion:

- macros that are read (`Ifdef`, `Ifndef`, `Defined`, `MacroExpands`, `#if` expressions)
  and their definitions (hash of the token sequence),
- macros that are defined/undefined,
- name lookups that reach declarations *outside* the header (typedef names, tags,
  enum constants) and their identity (file:line + hash),
- the number of tokens and declarations the header contributes.

Key = hash(header contents, macros read, declarations looked up).
Aggregates over the whole defconfig:

- share of tokens / declarations that would be cache hits,
- which headers have many variants (X-macros, `define_trace.h`,
  `-D` flags per directory) – candidates for special handling,
- per TU: number of `static inline` defined vs actually used
  (basis for lazy parsing and memoized code generation).

**Done when:** `results/header-redundancy.md` with the figures above and top-50 lists.

### 1.4 Thin client + proxy server (M3, M4)

Directory `ccs/`.

**Client (`ccs-cc`)**, C, statically linked (try `tools/include/nolibc` from the kernel tree):

- `connect()` to `$XDG_RUNTIME_DIR/ccs.sock` (or `$CCS_SOCKET`).
- Send argv, envp (or whitelisted variables), and fds 0/1/2 + an `O_PATH` fd
  for cwd via `sendmsg` + `SCM_RIGHTS`, over `SOCK_SEQPACKET`.
- Wait for the exit status, return it. On a signal: recreate it via `kill(getpid())`.
- Fallback: if the server does not answer, `execv` the real compiler (`$CCS_REAL_CC`).

**Server (`ccsd`)**, C or Rust:

- Verify `SO_PEERCRED` (same uid), socket `0600`.
- Thread pool; per request: `fchdir`/`openat`-based execution of the real
  compiler via `posix_spawn` with the client's fds, so diagnostics go straight to the
  make terminal.
- Log per request: argv, queue time, run time, exit status (feeds 1.2).
- `--verify` mode (preparation for Phase 2): runs the request twice and compares
  output files.

**Integration:** `make CC=ccs-cc HOSTCC=gcc ...` – no change of
`/usr/bin/cc` is needed in Phase 1. Kbuild's `cc-option`/`cc-version.sh`/`--version`
pass through transparently.

**Verification:**

- `vmlinux` built via proxy is byte-identical to the direct build.
- Boots in QEMU (`-kernel bzImage -initrd` with a busybox initramfs that writes a
  marker and powers off); script `bench/boot-test.sh`.
- Measure overhead: proxy vs direct, clean and no-op build. Expectation: < 2–3 %.
  Break out the pure client RTT (microbenchmark with `--version` requests).

**Done when:** `results/proxy.md`, and the boot test is green.

### 1.5 Report and decision (M5)

`results/fas1-rapport.md`:

- Summary of 1.2–1.4.
- **Go criteria for Phase 2** (proposed):
  - frontend ≥ 50 % of the compilation time per TU (at -O0), **and**
  - ≥ 80 % of header tokens are cache hits with the exact key, **and**
  - proxy overhead < 5 %.
- Choice of frontend base for Phase 2: **sparse** (kernel GNU C already covered),
  **aro**, or own. Criteria: GNU C coverage on the kernel, speed at
  `-fsyntax-only` level, how easy it is to make declarations immutable and
  shared.
- List of surprises and risks ahead of Phase 2.

## Direction for Phase 2 (discussed 2026-11-07)

Start from **TinyCC** (https://repo.or.cz/w/tinycc.git) instead of a new
frontend: preprocessor, code generation, assembler and ELF linker in one pass,
backends for arm64 and x86-64. Get it working on the kernel first, profile,
and then optimize step by step.

Known obstacles to map out at the end of Phase 1:

- Global state (`vtop`, `tok`, `file` …) and symbols that are mutated/freed
  at scope end – must be reworked for a shared header cache and threads.
- Lacks what the kernel requires: `always_inline` inlining, `__builtin_constant_p`
  after inlining, dead code elimination, parts of GNU C (e.g. `asm goto`).
- Linker: the idea is to build in a fast linker (mold/wild). Neither mold
  nor wild has full support for the kernel's linker scripts; lld (can be called
  as a library) or TinyCC's own linker are realistic alternatives for `vmlinux`.

## Directory structure

```
PLAN.md
env/        Dockerfile, setup.sh, fas1.config
bench/      baseline.sh, boot-test.sh, trace-till-chrome.py
analysis/   ppdeps/ (clang plugin), aggregation scripts
ccs/        client/, server/, protocol.h
results/    baseline.md, header-redundancy.md, proxy.md, fas1-rapport.md
```

## Risks

| Risk | Handling |
|------|----------|
| Container performance on macOS differs from bare-metal Linux | Measure relatively (before/after on the same machine); verify key figures on a Linux box if possible |
| `-ftime-trace` gives a misleading distribution for kernel flags | Cross-validate with `-fsyntax-only` and `perf` on `cc1`/`clang` |
| The Clang plugin's name-lookup tracking is incomplete | Start at the macro level (enough for an upper bound), add declaration dependencies iteratively; compare against (a) |
| Reproducible builds do not come out identical | Diff with `diffoscope`; document remaining differences |
| Kbuild behaves differently with a wrapper `CC` | Compare `.config`, `include/generated/` and `cmd` files between the direct and the proxy build |

## Order

1.1 → 1.2 and 1.4 in parallel (they share the wrapper/log mechanism) → 1.3 → 1.5.
1.3 is the phase's most important result; 1.4 is the most important infrastructure.
