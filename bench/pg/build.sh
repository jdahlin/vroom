#!/bin/bash
# PostgreSQL, native on macOS, built with meson + ninja: by clang (the
# reference) or with every C file compiled by vcc, then tested.
#
#   bench/pg/build.sh clang|vcc|vccd [setup|build|test|all]   (default: all)
#
#   vcc    CC is bench/pg/cc (Python): one vpp process per C file
#   vccd   CC is vcc's native shim (build/ccs/vcc) talking to a private vccd
#          started for the run: vccd compiles -c of .c itself, with the
#          header and declaration caches shared by all TUs, and runs
#          bench/pg/cc for everything else (VCC_DELEGATE: probes, links, any
#          TU vcc fails on; meson-private probes always).  A later ninja in
#          that tree needs a vccd with the same VCC_SOCK/VCC_DELEGATE.
#
#   setup  meson setup work/build-pg-$V (removed first); for vcc, the
#          generated pg_config.h is diffed against clang's
#   build  ninja -t clean, ninja -j 6 (ninja does not track the compiler),
#          meson install into work/pg-$V-inst (see below)
#   test   meson test: the whole suite (regress, isolation, ecpg, contrib,
#          test modules; TAP tests are off), log in work/build-pg-$V.test.log
#
# Source: work/postgres, REL_18_6 (cloned when missing).  Options:
# -Dauto_features=disabled -Dcassert=true, debugoptimized.  The install step
# is needed because SIP drops DYLD_LIBRARY_PATH when pg_regress runs psql
# through /bin/sh, so the binaries must find libpq at the real prefix.
#
# vcc failures during build go to work/build-pg-vcc.fail.jsonl; with
# VCC_PG_FALLBACK=1 clang compiles those files instead (see bench/pg/cc).
set -e
V=${1:?clang, vcc or vccd}
STEP=${2:-all}
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
W=$(cd "$ROOT/work" && pwd -P)
SRC=$W/postgres
B=$W/build-pg-$V
CLANG=${VCC_PG_CLANG:-/opt/homebrew/opt/llvm/bin/clang}
TAG=REL_18_6
J=${J:-6}

case $V in
clang) CC=$CLANG ;;
vcc) CC=$HERE/cc ;;
vccd) CC=$ROOT/build/ccs/vcc ;;
*) echo "build.sh: clang, vcc or vccd" >&2; exit 2 ;;
esac

if [ $V = vccd ]; then
	# sun_path is 104 bytes: a short directory, not the scratch TMPDIR
	VS=$(mktemp -d /tmp/pgvccd.XXXXXX)
	"$ROOT/build/ccs/vccd" --sock "$VS/sock" > "$W/build-pg-vccd.vccd.log" 2>&1 &
	VP=$!
	trap 'kill -TERM $VP 2>/dev/null; sleep 0.2; rm -rf "$VS"' EXIT
	export VCC_SOCK=$VS/sock VCC_DELEGATE=$HERE/cc VCC_DELEGATE_SKIP=meson-private
	for _ in 1 2 3 4 5 6 7 8 9 10; do [ -S "$VS/sock" ] && break; sleep 0.1; done
fi

setup() {
	[ -d "$SRC" ] || git clone -q --depth 1 -b $TAG https://git.postgresql.org/git/postgresql.git "$SRC"
	rm -rf "$B" "$W/build-pg-$V.setup-fail.jsonl"
	(cd "$SRC" && VCC_PG_LOG=$W/build-pg-$V.setup-fail.jsonl CC=$CC meson setup "$B" \
		-Dauto_features=disabled -Dcassert=true --buildtype=debugoptimized \
		-Dprefix="$W/pg-$V-inst" > "$W/build-pg-$V.setup.log" 2>&1) ||
		{ tail -20 "$W/build-pg-$V.setup.log"; exit 1; }
	if [ $V != clang ] && [ -f "$W/build-pg-clang/src/include/pg_config.h" ]; then
		echo "pg_config.h, clang vs vcc:"
		diff "$W/build-pg-clang/src/include/pg_config.h" "$B/src/include/pg_config.h" | grep '^[<>]' | grep -v PG_VERSION_STR || echo "  identical"
	fi
}

build() {
	rm -f "$W/build-pg-$V.fail.jsonl"
	ninja -C "$B" -t clean > /dev/null 2>&1 || true
	local t0=$(date +%s)
	VCC_PG_LOG=$W/build-pg-$V.fail.jsonl ninja -C "$B" -j $J > "$W/build-pg-$V.build.log" 2>&1 ||
		{ grep -A5 '^FAILED' "$W/build-pg-$V.build.log" | head -40; exit 1; }
	echo "build: $(grep -c 'Compiling C object' "$W/build-pg-$V.build.log") C files, $(( $(date +%s) - t0 )) s"
	[ -f "$W/build-pg-$V.fail.jsonl" ] && echo "vcc failed on $(wc -l < "$W/build-pg-$V.fail.jsonl") files (clang compiled them)"
	warnings
	meson install -C "$B" -q > /dev/null
}

# the compiler's warnings (file:line:col: message, flags and colors cut); for vcc, the ones
# the clang build does not give: vcc must print no warning clang does not
wlist() {
	grep 'warning:' "$1" | sed -e $'s/\x1b\[[0-9;]*m//g' -e $'s/\x1b\]8;;[^\x1b]*\x1b\\\\//g' -e 's/ \[[^]]*\]$//' | sort -u
}

warnings() {
	local L="$W/build-pg-$V.build.log" C="$W/build-pg-clang.build.log" n
	n=$(wlist "$L" | wc -l | tr -d ' ')
	if [ "$V" = clang ] || [ ! -f "$C" ]; then
		echo "warnings: $n"
		return
	fi
	echo "warnings: $n, not from clang: $(comm -13 <(wlist "$C") <(wlist "$L") | wc -l | tr -d ' ')"
	comm -13 <(wlist "$C") <(wlist "$L") | head -20
}

tests() {
	local t0=$(date +%s) st=0
	meson test -C "$B" -q --num-processes $J > "$W/build-pg-$V.test.log" 2>&1 || st=$?
	grep -E '^(Ok|Fail|Skipped|Timeout|Expected Fail|Unexpected Pass):' "$W/build-pg-$V.test.log" | tr -s ' ' | tr '\n' ' '
	echo "($(( $(date +%s) - t0 )) s)"
	grep -E ' (FAIL|ERROR|TIMEOUT) ' "$W/build-pg-$V.test.log" | grep -v '^>>>' || true
	return $st
}

case $STEP in
setup) setup ;;
build) build ;;
test) tests ;;
all) setup; build; tests ;;
*) echo "build.sh: setup, build, test or all" >&2; exit 2 ;;
esac
