#!/bin/sh
# vcc's -O3 optimizer: code speed and optimizer cost.
#
#   bench/vcc-opt.sh [runs]        (default 5; best of N per number)
#
# Speed of generated code: the workload is "compile vcc's own unity TUs
# (cc, pp, emit, as, obj) and the driver with -O2 -c" (sequential, one
# thread), run by
#   stage 1  build/ccs/vpp     (clang -O3)
#   stage 2  build/self/vpp    (vcc -O3: with vcc's -O3 optimizer)
#   stage 2' build/self-o2/vpp (vcc -O2: the baseline)
# Cost of optimizing: the same sources compiled by
#   clang -O0 -c vs clang -O3 -c     (clang's optimizer: the difference)
#   vpp -O2 -c   vs vpp -O3 -c       (vcc's -O3 work: the difference)
# Goal: vcc's optimizer cost <= 1/10 of clang's.
set -e
cd "$(dirname "$0")/../ccs"
RUNS=${1:-5}
make -s -j 6 vcc >/dev/null
U=../build/ccs/obj/unity
SRC="$U/cc.c $U/pp.c $U/emit.c $U/as.c $U/obj.c vcc/driver/ppdriver.c vcc/lex/lex.c vcc/ld/ld.c"
INC="$(pwd)/vcc/include"
FLAGS="-c -std=c11 -D_DARWIN_C_SOURCE"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

make -s self >/dev/null
make -s self SELF=../build/self-o2 SELF_OPT=-O2 >/dev/null

best() {	# best wall time in ms of "$@" over RUNS runs, each compiling all of SRC
	b=
	for r in $(seq "$RUNS"); do
		t0=$(perl -MTime::HiRes=time -e 'printf "%.0f", time*1000')
		for f in $SRC; do "$@" "-DVCC_INCDIR=\"$INC\"" "$f" -o "$TMP/o.o" || exit 1; done
		t1=$(perl -MTime::HiRes=time -e 'printf "%.0f", time*1000')
		t=$((t1 - t0))
		if [ -z "$b" ] || [ "$t" -lt "$b" ]; then b=$t; fi
	done
	echo "$b"
}

s1=$(best ../build/ccs/vpp $FLAGS -O2)
s2=$(best ../build/self/vpp $FLAGS -O2)
s2b=$(best ../build/self-o2/vpp $FLAGS -O2)
c0=$(best clang $FLAGS -O0 -w)
c3=$(best clang $FLAGS -O3 -w)
v2=$(best ../build/ccs/vpp $FLAGS -O2)
v3=$(best ../build/ccs/vpp $FLAGS -O3)

echo "code speed (workload: vpp -O2 -c on $(echo $SRC | wc -w | tr -d ' ') sources, ms, best of $RUNS)"
echo "  stage 1 (clang -O3):     $s1"
echo "  stage 2 (vcc -O3):       $s2   $(echo "scale=2; $s2/$s1" | bc)x stage 1"
echo "  stage 2 (vcc -O2, base): $s2b   $(echo "scale=2; $s2b/$s1" | bc)x stage 1"
echo "optimizer cost (ms)"
echo "  clang -O0 $c0, -O3 $c3: optimizer $((c3 - c0))"
echo "  vpp   -O2 $v2, -O3 $v3: -O3 work $((v3 - v2))"
d=$((v3 - v2)); [ "$d" -lt 1 ] && d=1
echo "  ratio clang/vcc: $(echo "scale=1; ($c3 - $c0)/$d" | bc)x (goal >= 10)"
