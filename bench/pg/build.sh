#!/bin/bash
# PostgreSQL, native on macOS, built with meson + ninja: by clang (the
# reference) or with every C file compiled by vcc (bench/pg/cc), then tested.
#
#   bench/pg/build.sh clang|vcc [setup|build|test|all]   (default: all)
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
V=${1:?clang or vcc}
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
*) echo "build.sh: clang or vcc" >&2; exit 2 ;;
esac

setup() {
	[ -d "$SRC" ] || git clone -q --depth 1 -b $TAG https://git.postgresql.org/git/postgresql.git "$SRC"
	rm -rf "$B" "$W/build-pg-$V.setup-fail.jsonl"
	(cd "$SRC" && VCC_PG_LOG=$W/build-pg-$V.setup-fail.jsonl CC=$CC meson setup "$B" \
		-Dauto_features=disabled -Dcassert=true --buildtype=debugoptimized \
		-Dprefix="$W/pg-$V-inst" > "$W/build-pg-$V.setup.log" 2>&1) ||
		{ tail -20 "$W/build-pg-$V.setup.log"; exit 1; }
	if [ $V = vcc ] && [ -f "$W/build-pg-clang/src/include/pg_config.h" ]; then
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
	meson install -C "$B" -q > /dev/null
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
