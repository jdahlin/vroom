#!/bin/zsh
# Sets up a kernel build directly on macOS (Apple Silicon), without a container.
# Idempotent: can be run again.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
KVER=v7.4-rc1
IMG="$ROOT/work.sparsebundle"
MNT="$ROOT/work"

# 1. Tools (GNU make/sed/coreutils etc.; llvm+lld for LLVM=1)
brew install make gnu-sed findutils coreutils grep bison flex pigz libelf llvm lld qemu

# 2. Case-sensitive volume (the kernel has files that differ only in case)
if [[ ! -d "$IMG" ]]; then
  hdiutil create -type SPARSEBUNDLE -fs 'Case-sensitive APFS' -volname vroom-work -size 80g "$IMG"
fi
mkdir -p "$MNT"
if ! mount | grep -q " on $MNT "; then
  hdiutil attach "$IMG" -mountpoint "$MNT" -nobrowse
fi

# 3. Host headers that macOS lacks (elf.h from musl, MIT license)
if [[ ! -f "$ROOT/env/host-include/elf.h" ]]; then
  curl -fsSL -o "$ROOT/env/host-include/elf.h" \
    'https://git.musl-libc.org/cgit/musl/plain/include/elf.h'
fi

# 4. The kernel
if [[ ! -d "$MNT/linux" ]]; then
  git clone --depth 1 --branch "$KVER" \
    https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git "$MNT/linux"
fi

# 5. Configurations
source "$ROOT/env/macos-env.sh"
cd "$MNT/linux"
kmake O=../build-tiny tinyconfig
kmake O=../build-def defconfig
./scripts/kconfig/merge_config.sh -m -O ../build-def ../build-def/.config "$ROOT/env/fas1.config"
kmake O=../build-def olddefconfig

echo "Done. Build with:  source env/macos-env.sh && cd work/linux && kmake O=../build-def -j\$(sysctl -n hw.ncpu) Image"
