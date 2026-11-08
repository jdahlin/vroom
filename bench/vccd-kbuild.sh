#!/bin/sh
# Cold kernel build through the vcc shims with a private vroomd + vccd.
#   bench/vccd-kbuild.sh OUT [VCC_CC=0 to forward -c to clang] [-j N]
# vccd's log goes to OUT.vccd.log (VCC_LOG=1); its counts are printed at the end.
set -e
R=$(cd "$(dirname "$0")/.." && pwd)
OUT=$(cd "$(dirname "$1")" && pwd)/$(basename "$1"); shift
J=14
[ "$1" = "-j" ] && { J=$2; shift 2; }
S=${TMPDIR:-/tmp}/vk.$$
B=$R/build/ccs
KSRC_DEFAULT=/Users/johandahlin/dev/vroom/work/linux-pf
[ "$(uname -s)" = Linux ] && { B=$R/build/linux/ccs; KSRC_DEFAULT=$R/work/linux; }
mkdir -p "$S"
"$B/vroomd" --sock "$S/vroomd.sock" --opt -O0 >"$OUT.vroomd.log" 2>&1 &
VP=$!
VCC_LOG=1 "$B/vccd" --sock "$S/vccd.sock" --backend-sock "$S/vroomd.sock" \
	--ld "$R/ccs/vroom-ld" >"$OUT.vccd.log" 2>&1 &
CP=$!
trap 'kill $VP 2>/dev/null; kill -TERM $CP 2>/dev/null; sleep 0.3; rm -rf "$S"' EXIT
sleep 0.5
rc=0
VCC_SOCK="$S/vccd.sock" VROOM_KSRC=${VROOM_KSRC:-$KSRC_DEFAULT} \
	python3 "$R/bench/kbuild.py" --config "$R/env/vroom.config" --out "$OUT" \
	--cc "$B/vcc" -j "$J" -- LD="$B/vld" || rc=$?
kill -TERM $CP; sleep 0.3
grep "^vccd:" "$OUT.vccd.log" | tail -1
grep -c "fallback to clang" "$OUT.vccd.log" || true
exit $rc
