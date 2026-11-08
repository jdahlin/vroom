#!/bin/sh
# Cold kernel build through the vcc shims with a private vroomd + vccd.
#   bench/vccd-kbuild.sh OUT [VCC_CC=0 to forward -c to clang] [-j N]
# VCC_FIXDEP=0: kbuild runs fixdep itself (default: vccd writes the .cmd files,
# patches/0008).
# vccd's log goes to OUT.vccd.log (VCC_LOG=1); its counts are printed at the end.
set -e
R=$(cd "$(dirname "$0")/.." && pwd)
OUT=$(cd "$(dirname "$1")" && pwd)/$(basename "$1"); shift
J=14
[ "$1" = "-j" ] && { J=$2; shift 2; }
S=${TMPDIR:-/tmp}/vk.$$
mkdir -p "$S"
"$R/build/ccs/vroomd" --sock "$S/vroomd.sock" --opt -O0 >"$OUT.vroomd.log" 2>&1 &
VP=$!
VCC_LOG=1 "$R/build/ccs/vccd" --sock "$S/vccd.sock" --backend-sock "$S/vroomd.sock" \
	--ld "$R/ccs/vroom-ld" >"$OUT.vccd.log" 2>&1 &
CP=$!
trap 'kill $VP 2>/dev/null; kill -TERM $CP 2>/dev/null; sleep 0.3; rm -rf "$S"' EXIT
sleep 0.5
rc=0
VCC_SOCK="$S/vccd.sock" VROOM_KSRC=${VROOM_KSRC:-/Users/johandahlin/dev/vroom/work/linux-pf} \
	python3 "$R/bench/kbuild.py" --config "$R/env/vroom.config" --out "$OUT" \
	--cc "$R/build/ccs/vcc" -j "$J" -- LD="$R/build/ccs/vld" \
	$([ "${VCC_FIXDEP:-1}" = 0 ] || echo KBUILD_VCC_FIXDEP=1) || rc=$?
kill -TERM $CP; sleep 0.3
grep "^vccd:" "$OUT.vccd.log" | tail -1
grep -c "fallback to clang" "$OUT.vccd.log" || true
exit $rc
