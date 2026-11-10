#!/bin/sh
# Compare vobjcopy/vnm with llvm-objcopy/llvm-nm on the kernel build's uses.
# Usage: bench/vobjcopy-cmp.sh [vmlinux.unstripped after sorttable]
# Outputs go to a temp dir; work/build-o0 is only read.
set -e
R=$(cd "$(dirname "$0")/.." && pwd)
W=$R/../../../work/build-o0; [ -d "$W" ] || W=$R/work/build-o0
B=$R/build/ccs
L=${LLVM:-/opt/homebrew/opt/llvm/bin}
IN=${1:-$W/vmlinux.unstripped}
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
ok=0; bad=0
chk() { if cmp -s "$1" "$2"; then ok=$((ok+1)); else echo "DIFF: $3"; bad=$((bad+1)); fi; }
both() { # name, args (IN/OUT substituted)
	n=$1; shift
	$L/llvm-objcopy $(echo "$@" | sed "s|@O|$T/r.$n|g") 2>/dev/null
	$B/vobjcopy $(echo "$@" | sed "s|@O|$T/m.$n|g")
	chk $T/r.$n $T/m.$n "objcopy $n"
}
both vmlinux --remove-section=.modinfo -w "--strip-unneeded-symbol=__mod_device_table__*" $IN @O
both Image -O binary -R .note -R .note.gnu.build-id -R .comment -S $T/r.vmlinux @O
$L/llvm-objcopy -O binary -j .modinfo --dump-section .modinfo=$T/r.modinfo $IN /dev/null
$B/vobjcopy -O binary -j .modinfo --dump-section .modinfo=$T/m.modinfo $IN /dev/null
chk $T/r.modinfo $T/m.modinfo modinfo
both vdso -S $W/arch/arm64/kernel/vdso/vdso.so.dbg @O
both vdso-g --strip-debug $W/arch/arm64/kernel/vdso/vdso.so.dbg @O
both pi-g --strip-debug $W/arch/arm64/kernel/pi/map_kernel.o @O
both pi-rm -R .comment $W/arch/arm64/kernel/pi/map_kernel.o @O
both pi-redef --redefine-sym map_range=mr --redefine-sym=memset=ms $W/arch/arm64/kernel/pi/map_kernel.o @O
printf hello > $T/blob; both pi-add --add-section .vroom=$T/blob $W/arch/arm64/kernel/pi/map_kernel.o @O
both pi-dl --add-gnu-debuglink=$T/blob $W/arch/arm64/kernel/pi/map_kernel.o @O
for f in idreg-override map_kernel map_range relocate lib-fdt lib-fdt_ro; do
	x=; case $f in lib-*) x=--prefix-alloc-sections=.init;; esac
	both $f --prefix-symbols=__pi_ --remove-section=.note.gnu.property $x $W/arch/arm64/kernel/pi/$f.o @O
done
$L/llvm-nm -p -S $IN > $T/r.nm; $B/vnm -p -S $IN > $T/m.nm; chk $T/r.nm $T/m.nm "nm -p -S"
for o in -u -g -U --undefined-only --extern-only --defined-only; do
  $L/llvm-nm $o $IN > $T/r.nm; $B/vnm $o $IN > $T/m.nm; chk $T/r.nm $T/m.nm "nm $o"; done
V=$W/arch/arm64/kernel/vdso/vdso.so.dbg
$L/llvm-nm $V > $T/r.vnm; $B/vnm $V > $T/m.vnm; chk $T/r.vnm $T/m.vnm "nm vdso"
echo "$ok identical, $bad different"
[ $bad = 0 ]
