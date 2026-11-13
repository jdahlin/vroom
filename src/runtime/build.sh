#!/bin/sh
# Build libvccrt.a, vcc's run-time support library for x86_64-w64-mingw32
# (src/runtime/, doc/PLAN-win64.md step WIN7R), into OUTDIR.
#
#   sh src/runtime/build.sh OUTDIR      (make -C src vccrt: OUTDIR = $(OUT)/vccrt)
#
# Parameters (environment):
#   CC      the compiler for the target.  Default: the oracle clang, until
#           vcc's Windows x64 code generator is in; then
#           CC="build/linux/src/vcc --target=x86_64-w64-mingw32" is the only
#           change (the sources are the C subset vcc compiles: GNU C11 with
#           __int128, _Float16 and __float128, no intrinsics, no inline asm).
#   AR      the archiver.  Default llvm-ar: vcc's own `var` writes the symbol
#           table only for ELF members, and a COFF archive needs one.
#   CFLAGS  extra flags.
#   SRC     the sources (default: every .c and .S here).
#
# The library must need nothing outside itself: the sources use no operation
# a compiler for the target lowers to one of these helpers (no 128-bit
# division, no __int128 <-> floating conversion, no __float128 arithmetic),
# and -ffreestanding -fno-builtin keeps memcpy/memset calls out.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
out=${1:?usage: build.sh OUTDIR}
llvm=${VROOM_LLVM_BIN:-/usr/lib/llvm-23/bin}
CC=${CC:-$llvm/clang --target=x86_64-w64-mingw32}
AR=${AR:-$llvm/llvm-ar}
CFLAGS=${CFLAGS:-}
SRC=${SRC:-$(cd "$here" && ls *.c *.S 2>/dev/null || true)}
mkdir -p "$out/obj"
objs=
for f in $SRC; do
	o="$out/obj/${f%.*}.o"
	# shellcheck disable=SC2086
	$CC -O2 -ffreestanding -fno-builtin -fno-stack-protector -ffp-contract=off -nostdinc -I "$here" \
		-Wall -Wextra -Werror $CFLAGS -c "$here/$f" -o "$o"
	objs="$objs $o"
done
rm -f "$out/libvccrt.a.tmp"
# shellcheck disable=SC2086
$AR rcsD "$out/libvccrt.a.tmp" $objs
mv "$out/libvccrt.a.tmp" "$out/libvccrt.a"
