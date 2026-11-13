# Goal: cold kernel build < 30 s, -O0, that boots Debian to userspace – achieved

**Date:** 2026-11-07 · **Machine:** M1 Pro (8P+2E), 32 GB, macOS 27.2
**Kernel:** Linux v7.4-rc1 + `patches/0001-arm64-build-with-O0.patch`, arm64, **-O0**
**Command:** `./vroom-build.sh` (stops/starts vroomd, `rm -rf` of the build directory, builds `Image`)
**Boot:** `python3 bench/boot-test.py --kernel work/build-cold/arch/arm64/boot/Image`

| Run | Time incl. server start | Userspace (QEMU hvf, Debian 13.7 nocloud) |
|-----|------------------------|-------------------------------------------|
| 1 | **23.8 s** | `is-system-running` = running, 0 failed units |
| 2 | **23.5 s** | running, 0 failed units |

- No build cache: the build directory is removed, the server is restarted and has no state.
  Inputs are the source tree and `env/vroom.config` (537 built-in options, standard irqchips).
- All 838 kernel C files are built with `-O0` (`.vroom/opt.log`). 833 use the shared
  PCH, and 5 are built without PCH after a failed attempt with it.
- The PCH is used with full validation: no `-fno-validate-pch`, `-Wno-*` or
  `KBUILD_MODNAME` tricks.
- The build's only warnings are 18 `-Wframe-larger-than`, because -O0 gives large stack frames.
- Userspace is verified without login: `systemd.run=` starts a transient service after
  `multi-user.target`. It writes `uname -r`, `/etc/debian_version`,
  `systemctl is-system-running` and failed units to the serial console.

## How -O0 became possible

The upstream kernel does not build with -O0 on arm64. A `make -k` build gave 454 of 828 files
with errors, all from `"i"` operands in inline asm that get their constants through
`__always_inline` functions. Constant propagation requires mem2reg, which -O0 does not do.

**Kernel patch** (`patches/0001-arm64-build-with-O0.patch`, 10 files, +119/−97). No
change of semantics; the same code becomes constant already in the frontend:

| Change | Why |
|--------|-----|
| `alternative_has_cap_{likely,unlikely}`, `arch_static_branch{,_jump}`, `static_key_{true,false}`, `cpus_have_final{,_boot}_cap` → statement-expression macros | the `"i"` operand becomes a constant already in the frontend |
| `sched_feat(x)` selects the branch through an enum constant instead of an `__always_inline` function per feature | same for `sched_feat_keys[]` |
| `BUILD_BUG()` → `__builtin_trap()` when `!__OPTIMIZE__` | noreturn, so `-Werror=return-type` passes. The branch is dead (a -O2 build would break otherwise). `compiletime_assert` is otherwise unchanged (upstream: no-op without `__OPTIMIZE__`) |
| `kvm_host.h`: `default: BUILD_BUG_ON(1)` → `BUILD_BUG()` | same |
| `memblock_alloc_{raw,low,node}`, `set_smp_ipi_range`, `do_trace_initcall_*` → `__always_inline` | wrappers around `__init` functions. Without inlining they end up in `.text` (modpost: section mismatch) |

**`ccs/vroom-ld`** – a small linker wrapper for the kernel:
- At -O0 some branches remain that -O2 proves dead with the current config. Examples
  are SME/GCS signal code, IOMMU-DMA, THP and link-time asserts such as `____wrong_branch_error`.
  They reference 92 symbols that do not exist.
- If lld reports *only* undefined symbols, a stub object is generated with 64 zero bytes
  per symbol in `.data`, and the link is rerun.
- `.data` is writable, so static keys work, and NX, so a call gives an
  exception in the style of `BUG()`.
- The list is saved in `vmlinux.unstripped.vroom-stubs`.

**`vroomd`:** the PCH is built with the same `-O` as the files. Previously `__OPTIMIZE__`
was carried over from -O2 into all files, and that is why my first -O0 attempt seemed
hopeless. `net/tcp.h` was removed from the prefix, because `flow_offload.h` uses
`KBUILD_MODNAME` in an inline function.

## The way there

| Step | Time | What |
|------|------|------|
| defconfig, all `=m` → `=y` | 575.6 s | baseline, 9 155 C files |
| Minimal config for QEMU virt + Debian | 97 s | 1 097 files (`env/vroom-min.config`) |
| Trimmed config + saved `.config` | 67 s | no KALLSYMS/BPF/io_uring |
| `vroomd` zygote + `vroom-cc` | 53 s | clang's startup 44 ms → 6 ms |
| Shared PCH in the server | 40 s | headers parsed once per build |
| (-O1, irqchip patch, `-Wno-ignored-attributes`) | 22.8 s | intermediate stage, now removed |
| Network + loopback back | 24.8 s | required for systemd-resolved |
| **-O0 + vroom-ld, validated PCH, standard irqchips** | **23.5–23.8 s** | final state |

Without PCH, a -O0 build takes 50.8 s.

## Remaining to gain

- The preparation before the first kernel file takes ~6 s: Kconfig runs ~130 flag tests serially
  (~20 ms each in sh/mkdir/rm), plus asm-offsets etc.
- Assembler jobs (`-cc1as`) are still run by real clang (~45 ms startup).
- Compiler and linker are clang/lld via the server, not our own.
