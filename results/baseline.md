# Baseline – the first measurement

**Date:** 2026-11-07
**Machine:** Apple M1 Pro, 10 cores, 32 GB RAM, macOS 27.2
**Tools:** Homebrew clang/lld 23.1.4 (`ARCH=arm64 LLVM=1`), GNU make 4.4.1
**Kernel:** v7.4-rc1 (`6628e7d4c`), `defconfig` + `env/fas1.config`
(no debug info, no LTO/CFI/KASAN, `MODULES=n` → all `=m` are built in)
**Command:** `kmake O=../build-def -j10 Image`

## Clean build (one run, cold)

| Metric | Value |
|--------|-------|
| Wall time | **575.6 s** (9 min 36 s) |
| User | 3 746 s |
| Sys | 754 s |
| Average load | 7.8 of 10 cores |
| C files (`CC`) | 9 155 |
| Assembler files (`AS`) | 77 |
| CPU time per C file (average) | ≈ 0.49 s (user+sys / number of CC) |
| `vmlinux` | 112 MB |
| `Image` | 98 MB |

## Observations

- **Sys time is high, 17 % of CPU time.** Process start, `exec` and
  file system calls are expensive on macOS. This argues for the client/server model.
- The average load of 7.8 of 10 shows that there are serial parts (startup,
  host tools, linking, kallsyms) that do not fill all cores.
- The figure is for a single cold build and includes building the host tools.
  Step 1.2 requires the best of 3–5 runs, plus no-op and incremental builds.
