#!/usr/bin/env bash
# Build FFmpeg 8.1.3 and run its sample-free FATE suite in both x86-64 modes.
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
work=${VROOM_WORK:-$root/work}/ffmpeg-linux
clang=${CLANG:-clang}
vpp=${VPP:-$root/build/linux/src/vpp}
resource=$("$clang" -print-resource-dir)
multiarch=$(gcc -print-multiarch)
mkdir -p "$work"
cd "$work"
archive=ffmpeg-8.1.3.tar.xz
if [[ ! -f $archive ]]; then
    curl -fL "https://ffmpeg.org/releases/$archive" -o "$archive.part"
    mv "$archive.part" "$archive"
fi
echo "7138d28c96d9d3e3af4ee3d8cad72741f8ffb40da90c1112235dea3ecd3178a3  $archive" | sha256sum -c -
tar xf "$archive"
# Keep release-tarball tools from discovering the surrounding vroom Git repo.
export GIT_CEILING_DIRECTORIES=$PWD
src=$PWD/ffmpeg-8.1.3
# Configure discovers the reference feature set. Make invokes vpp directly for
# all target C and host C. The configured linker driver receives objects.
# -fno-vir: FFmpeg's -O3 with the direct emitter (doc/OPTIMIZATION.md)
cc="$vpp --target=x86_64-linux-gnu -fno-vir -isystem $resource/include -isystem /usr/include/$multiarch -isystem /usr/include"
for mode in nasm noasm; do
    mkdir -p "$mode"
    (
        cd "$mode"
        flags=()
        if [[ $mode == noasm ]]; then flags+=(--disable-asm); fi
        "$src/configure" --cc="$clang" --disable-autodetect --disable-debug --disable-doc "${flags[@]}" >configure.log 2>&1
        expected=1
        if [[ $mode == noasm ]]; then expected=0; fi
        for feature in HAVE_X86ASM HAVE_SSE2_INLINE ARCH_X86_64; do
            grep -qx "#define $feature $expected" config.h
        done
        make clean >clean.log 2>&1
        make -j6 CC="$cc" HOSTCC="$cc" >build.log 2>&1
        make -kj6 CC="$cc" HOSTCC="$cc" fate >fate.log 2>&1
        echo "$mode: $(grep -c '^TEST ' fate.log) FATE tests passed"
    )
done
