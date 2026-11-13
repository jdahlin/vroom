#!/bin/sh
# vcc-f80.sh [N]: lib/f128.h's x87 extended folding against this host's x87
# long double (src/tests/f80/f80host.c); x86-64 hosts only.
set -e
B=$(dirname "$0")/..
O=${TMPDIR:-/tmp}/vcc-f80.$$
trap 'rm -f "$O"' EXIT
${CLANG:-clang} -O2 -o "$O" "$B/src/tests/f80/f80host.c"
"$O" "$@"
