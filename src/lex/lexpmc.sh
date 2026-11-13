#!/bin/sh
# PMU counters for the lexer (needs root):  sudo src/lex/lexpmc.sh [BINARY...]
# Default: build/src/lextest.  Each binary: full / NOINTERN=1, two event sets.
cd "$(dirname "$0")/../../.." || exit 1
[ $# -eq 0 ] && set -- build/src/lextest
E1=L1D_CACHE_MISS_LD_NONSPEC,L1D_CACHE_MISS_ST_NONSPEC,L1D_TLB_MISS_NONSPEC,L2_TLB_MISS_DATA
E2=BRANCH_MISPRED_NONSPEC,BRANCH_COND_MISPRED_NONSPEC,ST_MEM_ORDER_VIOL_LD_NONSPEC,MAP_STALL,FLUSH_RESTART_OTHER_NONSPEC
for b in "$@"; do
	for m in 0 1; do
		for e in $E1 $E2; do
			echo "== $b NOINTERN=$m"
			PMC=$e NOINTERN=$m $b core work/lex-files.txt 5
		done
	done
done 2>&1 | tee work/lexpmc.txt
