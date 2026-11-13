#!/bin/bash
# PostgreSQL, native on Linux or macOS, built with meson + ninja: by clang (the
# reference) or with every C file compiled by vcc, then tested.
#
#   bench/pg/build.sh clang|vcc|vccd [setup|build|test|corpus|all]   (default: all)
#
#   vcc    CC is bench/pg/cc (Python): one vpp process per C file
#   vccd   CC is vcc's native shim (build/src/vcc) talking to a private vccd
#          started for the run: vccd compiles -c of .c itself, with the
#          header and declaration caches shared by all TUs, and runs
#          bench/pg/cc for everything else (VCC_DELEGATE: probes, links, any
#          TU vcc fails on; meson-private probes always).  A later ninja in
#          that tree needs a vccd with the same VCC_SOCK/VCC_DELEGATE.
#
#   setup  "$MESON" setup work/build-pg-$V (removed first); for vcc, the
#          generated pg_config.h is diffed against clang's
#   build  ninja -t clean, ninja -j 6 (ninja does not track the compiler),
#          "$MESON" install into work/pg-$V-inst (see below)
#   test   meson test: the whole suite (regress, isolation, ecpg, contrib,
#          test modules; enable TAP with VCC_PG_TAP_TESTS=enabled), with full logs
#   corpus prepare every C object in compile_commands.json, including developer
#          targets and generated test inputs, before speed.py.
#
# Paths can be isolated with VROOM_WORK or VCC_PG_SOURCE/BUILD/PREFIX/LOG_BASE.
# VPP selects the compiler; LLVM or VCC_PG_CLANG selects the reference.
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
W=${VROOM_WORK:-$ROOT/work}
mkdir -p "$W"
W=$(cd "$W" && pwd -P)
SRC=${VCC_PG_SOURCE:-$W/postgres}
B=${VCC_PG_BUILD:-$W/build-pg-$V}
L=${VCC_PG_LOG_BASE:-$W/build-pg-$V}
PREFIX=${VCC_PG_PREFIX:-$W/pg-$V-inst}
CB=${VCC_PG_CLANG_BUILD:-$W/build-pg-clang}
CL=${VCC_PG_CLANG_LOG_BASE:-$W/build-pg-clang}
if [ "$(uname -s)" = Darwin ]; then
    DEFAULT_BIN=$ROOT/build/src
    DEFAULT_CLANG=/opt/homebrew/opt/llvm/bin/clang
else
    DEFAULT_BIN=$ROOT/build/linux/src
    DEFAULT_CLANG=$(command -v clang || true)
fi
BIN=${VCC_BIN:-$DEFAULT_BIN}
CLANG=${VCC_PG_CLANG:-${LLVM:+$LLVM/bin/clang}}
CLANG=${CLANG:-$DEFAULT_CLANG}
export VCC_PG_CLANG=$CLANG VPP=${VPP:-$BIN/vpp}
MESON=${MESON:-meson}
NINJA=${NINJA:-ninja}
TAG=REL_18_6
J=${J:-6}

case $V in
clang) CC=$CLANG ;;
vcc) CC=$HERE/cc ;;
vccd) CC=$BIN/vcc ;;
*) echo "build.sh: clang, vcc or vccd" >&2; exit 2 ;;
esac

if [ $V = vccd ]; then
	# sun_path is 104 bytes: a short directory, not the scratch TMPDIR
	VS=$(mktemp -d /tmp/pgvccd.XXXXXX)
	"$BIN/vccd" --sock "$VS/sock" > "$L.vccd.log" 2>&1 &
	VP=$!
	trap 'kill -TERM $VP 2>/dev/null; sleep 0.2; rm -rf "$VS"' EXIT
	export VCC_SOCK=$VS/sock VCC_DELEGATE=$HERE/cc VCC_DELEGATE_SKIP=meson-private
	for _ in 1 2 3 4 5 6 7 8 9 10; do [ -S "$VS/sock" ] && break; sleep 0.1; done
fi

setup() {
	[ -d "$SRC" ] || git clone -q --depth 1 -b $TAG https://git.postgresql.org/git/postgresql.git "$SRC"
	rm -rf "$B" "$L.setup-fail.jsonl"
	(cd "$SRC" && VCC_PG_LOG=$L.setup-fail.jsonl CC=$CC "$MESON" setup "$B" \
		-Dauto_features=disabled -Dcassert=true -Dtap_tests="${VCC_PG_TAP_TESTS:-disabled}" --buildtype=debugoptimized \
		-Dprefix="$PREFIX" > "$L.setup.log" 2>&1) ||
		{ tail -20 "$L.setup.log"; exit 1; }
	# PostgreSQL TAP checks exact data-directory modes; do not inherit a
	# shared workspace parent's setgid bit into newly created test fixtures.
	mkdir -p "$B/testrun"
	chmod g-s "$B" "$B/testrun"
	if [ $V != clang ] && [ -f "$CB/src/include/pg_config.h" ]; then
		echo "pg_config.h, clang vs vcc:"
		diff -u "$CB/src/include/pg_config.h" "$B/src/include/pg_config.h" > "$L.config.diff" || true
		cat "$L.config.diff"
	fi
}

build() {
	rm -f "$L.fail.jsonl"
	"$NINJA" -C "$B" -t clean > /dev/null 2>&1 || true
	local t0=$(date +%s)
	VCC_PG_LOG=$L.fail.jsonl "$NINJA" -C "$B" -k 0 -j $J > "$L.build.log" 2>&1 ||
		{ grep -A5 '^FAILED' "$L.build.log" | head -40; exit 1; }
	echo "build: $(grep -c 'Compiling C object' "$L.build.log") C files, $(( $(date +%s) - t0 )) s"
	if [ -s "$L.fail.jsonl" ]; then
		echo "vcc compiler failures recorded in $L.fail.jsonl; this is not a successful vcc build" >&2
		return 1
	fi
	warnings
	"$MESON" install -C "$B" -q > /dev/null
}

# the compiler's warnings (file:line:col: message, flags and colors cut); for vcc, the ones
# the clang build does not give: vcc must print no warning clang does not
wlist() {
	grep 'warning:' "$1" | sed -e $'s/\x1b\[[0-9;]*m//g' -e $'s/\x1b\]8;;[^\x1b]*\x1b\\\\//g' -e 's/ \[[^]]*\]$//' | sort -u
}

warnings() {
	local C="$CL.build.log" n
	local L="$L.build.log"
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
	VCC_PG_LOG=$L.test-fail.jsonl "$MESON" test -C "$B" -q --num-processes $J > "$L.test.log" 2>&1 || st=$?
	grep -E '^(Ok|Fail|Skipped|Timeout|Expected Fail|Unexpected Pass):' "$L.test.log" | tr -s ' ' | tr '\n' ' '
	echo "($(( $(date +%s) - t0 )) s)"
	grep -E ' (FAIL|ERROR|TIMEOUT) ' "$L.test.log" | grep -v '^>>>' || true
	return $st
}

# Prepare every C object in the exact compile database, including developer
# targets and test-only generated inputs. Never filter out failed/missing jobs.
corpus() {
    rm -f "$L.corpus-fail.jsonl"
    VCC_PG_LOG=$L.corpus-fail.jsonl python3 - "$B" "$NINJA" "$J" > "$L.corpus.log" 2>&1 <<'PYCODE'
import json, pathlib, subprocess, sys
build, ninja, workers = sys.argv[1:]
entries = json.loads((pathlib.Path(build)/'compile_commands.json').read_text())
outputs = [entry['output'] for entry in entries if entry['file'].endswith('.c')]
if not outputs: raise SystemExit('empty C compile corpus')
raise SystemExit(subprocess.run([ninja, '-C', build, '-k', '0', '-j', workers, *outputs]).returncode)
PYCODE
    [ ! -s "$L.corpus-fail.jsonl" ] || { echo "vcc corpus failures recorded in $L.corpus-fail.jsonl" >&2; return 1; }
}

case $STEP in
setup) setup ;;
build) build ;;
test) tests ;;
corpus) corpus ;;
all) setup; build; tests ;;
*) echo "build.sh: setup, build, test, corpus or all" >&2; exit 2 ;;
esac
