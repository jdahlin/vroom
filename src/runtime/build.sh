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
#   AR      the archiver.  Default: vcc's own `var` in VCC_BIN (default
#           OUTDIR's parent, the build directory under make), which writes
#           COFF archives byte-identical to llvm-ar's (bench/var-cmp.py coff);
#           this script then runs it against a private vccd of its own, so
#           a long-running shared one cannot be an older build.  AR=llvm-ar
#           (or any ar) still works.
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
CFLAGS=${CFLAGS:-}
SRC=${SRC:-$(cd "$here" && ls *.c *.S 2>/dev/null || true)}
mkdir -p "$out/obj"
if [ -z "${AR:-}" ]; then
	bin=${VCC_BIN:-$(cd "$out/.." && pwd)}
	AR=$bin/var
	if [ ! -x "$AR" ] || [ ! -x "$bin/vccd" ]; then
		echo "build.sh: no $bin/var or $bin/vccd (make -C src vcc, set VCC_BIN, or AR=llvm-ar)" >&2
		exit 1
	fi
	vdir=$(mktemp -d "${TMPDIR:-/tmp}/vccrt-vccd.XXXXXX")
	"$bin/vccd" --sock "$vdir/s" 2>"$vdir/log" &
	vpid=$!
	trap 'kill $vpid 2>/dev/null; wait $vpid 2>/dev/null; rm -rf "$vdir"' EXIT
	i=0
	while [ ! -S "$vdir/s" ]; do
		i=$((i + 1))
		if [ $i -gt 200 ] || ! kill -0 $vpid 2>/dev/null; then
			echo "build.sh: vccd did not start" >&2
			cat "$vdir/log" >&2
			exit 1
		fi
		sleep 0.05
	done
	VCC_SOCK=$vdir/s
	export VCC_SOCK
fi
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
