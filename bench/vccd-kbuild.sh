#!/bin/sh
# Cold kernel build through the vcc shims with a private vroomd + vccd.
#   bench/vccd-kbuild.sh OUT [VCC_CC=0 to forward -c to clang] [-j N] [make args...]
# VCC_FIXDEP=0: kbuild runs fixdep itself (default: vccd writes the .cmd files,
# patches/0008).  VCC_AR=0: llvm-ar as AR (default: vcc's ar in vccd, ar.c).
# KB_SHELL: SHELL/CONFIG_SHELL (default /bin/dash; KB_SHELL= for make's /bin/sh,
# which is bash on macOS).  VSH=1 (default): make's SHELL is vsh (vccd runs kbuild's object
# and archive recipes itself, rsh.c; the rest goes to $KB_SHELL), CONFIG_SHELL
# stays $KB_SHELL.
# KB_OPTS: more kbuild.py options (e.g. "--keep --dotconfig F" for a rebuild).
# vccd's log goes to OUT.vccd.log (VCC_LOG=1); its counts are printed at the end.
set -e
R=$(cd "$(dirname "$0")/.." && pwd)
OUT=$(cd "$(dirname "$1")" && pwd)/$(basename "$1"); shift
J=14
[ "$1" = "-j" ] && { J=$2; shift 2; }
S=${TMPDIR:-/tmp}/vk.$$
B=$R/build/ccs
KSRC_DEFAULT=/Users/johandahlin/dev/vroom/work/linux-pf
[ "$(uname -s)" = Linux ] && { B=$R/build/linux/ccs; KSRC_DEFAULT=$R/work/linux-pf; }
mkdir -p "$S"
"$B/vroomd" --sock "$S/vroomd.sock" --opt -O0 >"$OUT.vroomd.log" 2>&1 &
VP=$!
VCC_LOG=1 "$B/vccd" --sock "$S/vccd.sock" --backend-sock "$S/vroomd.sock" \
	--ld "$R/ccs/phase1/vroom-ld" >"$OUT.vccd.log" 2>&1 &
CP=$!
trap 'kill $VP 2>/dev/null; kill -TERM $CP 2>/dev/null; sleep 0.3; rm -rf "$S"' EXIT
sleep 0.5
rc=0
KSH=${KB_SHELL-/bin/dash}
VSHARGS=
if [ "${VSH:-1}" = 1 ]; then
	export VSH_REAL=${KSH:-/bin/sh}
	KSH=$B/vsh/sh
	VSHARGS="CONFIG_SHELL=$VSH_REAL"
fi
VCC_SOCK="$S/vccd.sock" VROOM_KSRC=${VROOM_KSRC:-$KSRC_DEFAULT} \
	python3 "$R/bench/kbuild.py" --config "$R/env/vroom.config" --out "$OUT" \
	--cc "$B/vcc" -j "$J" $KB_OPTS \
	$([ "${VCC_AR:-1}" = 0 ] || echo --ar "$B/var") \
	$([ -n "$KSH" ] && echo --shell "$KSH") \
	-- LD="$B/vld" \
	$([ "${VCC_FIXDEP:-1}" = 0 ] || echo KBUILD_VCC_FIXDEP=1) $VSHARGS "$@" || rc=$?
kill -TERM $CP; sleep 0.3
grep "^vccd:" "$OUT.vccd.log" | tail -4
grep -c "fallback to clang" "$OUT.vccd.log" || true
exit $rc
