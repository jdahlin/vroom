#!/usr/bin/env python3
"""Shared bounded mask-flow contracts, checked against independent path queries."""
import argparse
from pathlib import Path
import subprocess
import tempfile

from hostenv import R, llvm

SOURCE = r'''
#include <assert.h>
#include "vcc/emit/flow/graph.c"
#include "vcc/emit/flow/maskflow.c"

/* Independent oracle: for one bit, find a path to a use or live exit before
   any definition kills the old value. It reads the original edge list, not
   CSR adjacency or the solver's work list and transfer iteration. */
static bool path_live(uint32_t start, unsigned bit, uint32_t n,
                      const em_flow_edge *edges, uint32_t ne,
                      const em_flow_transfer *t)
{
    uint8_t seen[16] = {0};
    uint32_t stack[16], count = 1;
    uint64_t mask = UINT64_C(1) << bit;
    assert(n <= 16);
    stack[0] = start;
    seen[start] = 1;
    while (count) {
        uint32_t b = stack[--count];
        if (t[b].use & mask) return true;
        if (t[b].def & mask) continue;
        if (t[b].exit & mask) return true;
        for (uint32_t j = 0; j < ne; ++j) {
            uint32_t dest = edges[j].to;
            if (edges[j].from == b && !seen[dest]) {
                seen[dest] = 1;
                stack[count++] = dest;
            }
        }
    }
    return false;
}
static void compare(const em_flow_live *live, uint32_t n,
                    const em_flow_edge *edges, uint32_t ne,
                    const em_flow_transfer *t)
{
    assert(live->valid);
    for (uint32_t b = 0; b < n; ++b) {
        uint64_t in = 0, out = t[b].exit;
        for (unsigned bit = 0; bit < 64; ++bit) {
            uint64_t mask = UINT64_C(1) << bit;
            if (path_live(b, bit, n, edges, ne, t)) in |= mask;
            for (uint32_t j = 0; j < ne; ++j)
                if (edges[j].from == b && path_live(edges[j].to, bit, n, edges, ne, t))
                    out |= mask;
        }
        assert(live->in[b] == in && live->out[b] == out);
    }
}
static void adjacency(const em_flow_graph *g, const em_flow_edge *edges, uint32_t ne)
{
    assert(g->valid && g->has_pred && g->edges == ne);
    assert(g->succ_off[g->blocks] == ne && g->pred_off[g->blocks] == ne);
    for (uint32_t b = 0; b < g->blocks; ++b) {
        for (uint32_t dest = 0; dest < g->blocks; ++dest) {
            uint32_t expected = 0, succ = 0, pred = 0;
            for (uint32_t j = 0; j < ne; ++j)
                expected += edges[j].from == b && edges[j].to == dest;
            for (uint32_t j = g->succ_off[b]; j < g->succ_off[b + 1]; ++j)
                succ += g->succ[j] == dest;
            for (uint32_t j = g->pred_off[dest]; j < g->pred_off[dest + 1]; ++j)
                pred += g->pred[j] == b;
            assert(succ == expected && pred == expected);
        }
    }
}
static uint32_t random_word(uint32_t *state)
{
    *state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
    return *state;
}
int main(void)
{
    em_flow_graph graph = {0};
    em_flow_live live = {0};
    /* Diamond with a loop, duplicate edge, disconnected self-loop and exit. */
    const em_flow_edge edges[] = {{0,1},{0,2},{1,3},{2,3},{3,1},{4,4},{0,1}};
    const em_flow_transfer transfers[] = {
        {1,2,0}, {2,4,0}, {4,2,0}, {8,1,16}, {32,64,128}, {256,512,1024}
    };
    assert(em_flow_build(&graph,6,edges,7,true));
    adjacency(&graph,edges,7);
    assert(em_flow_liveness(&graph,transfers,&live,10000));
    compare(&live,6,edges,7,transfers);
    assert(live.in[0]==29 && live.out[0]==30);
    assert(live.in[1]==26 && live.in[2]==28 && live.in[3]==26);
    assert(live.in[4]==160 && live.in[5]==1280);
    uint32_t exact = (uint32_t)live.work;
    assert(exact > 1);
    /* A truncated analysis cannot publish partial facts, even after success.
       Reusing the same storage must recover the complete answer afterward. */
    assert(!em_flow_liveness(&graph,transfers,&live,0) && !live.valid && !live.work);
    assert(!em_flow_liveness(&graph,transfers,&live,1) && !live.valid && live.work<=1);
    assert(!em_flow_liveness(&graph,transfers,&live,exact-1) && !live.valid && live.work<=exact-1);
    assert(em_flow_liveness(&graph,transfers,&live,exact) && live.work==exact);
    compare(&live,6,edges,7,transfers);
    /* Invalid topology must invalidate a previous graph rather than expose
       its old adjacency to a new function. */
    const em_flow_edge invalid[] = {{0,6}};
    assert(!em_flow_build(&graph,6,invalid,1,true) && !graph.valid);
    assert(!em_flow_liveness(&graph,transfers,&live,10000) && !live.valid);
    assert(!em_flow_build(&graph,1,NULL,1,true) && !graph.valid);
    assert(!em_flow_build(&graph,0,NULL,0,true) && !graph.valid);
    assert(!em_flow_build(&graph,UINT32_MAX,NULL,0,true) && !graph.valid);
    assert(em_flow_build(&graph,6,edges,7,false) && !graph.has_pred);
    assert(!em_flow_liveness(&graph,transfers,&live,10000) && !live.valid);
    /* Single block, no edges: an exit value killed in the block is not live
       on entry, whereas an explicit use remains live even when also defined. */
    const em_flow_transfer singleton[] = {{UINT64_C(1)<<63,~UINT64_C(0),7}};
    assert(em_flow_build(&graph,1,NULL,0,true));
    assert(em_flow_liveness(&graph,singleton,&live,10000));
    compare(&live,1,NULL,0,singleton);
    assert(live.in[0]==(UINT64_C(1)<<63) && live.out[0]==7);
    /* Bounded deterministic coverage changes numbering, loops, duplicate
       edges and use/kill/exit combinations without copying solver logic. */
    uint32_t random = 1234567;
    for (unsigned trial = 0; trial < 24; ++trial) {
        em_flow_edge generated[40];
        em_flow_transfer t[8];
        uint32_t n = 1 + random_word(&random)%8, ne = random_word(&random)%40;
        for (uint32_t j=0;j<ne;++j) {
            generated[j].from=random_word(&random)%n;
            generated[j].to=random_word(&random)%n;
        }
        for (uint32_t b=0;b<n;++b) {
            t[b].use=((uint64_t)random_word(&random)<<32)|random_word(&random);
            t[b].def=((uint64_t)random_word(&random)<<32)|random_word(&random);
            t[b].exit=((uint64_t)random_word(&random)<<32)|random_word(&random);
        }
        assert(em_flow_build(&graph,n,generated,ne,true));
        adjacency(&graph,generated,ne);
        assert(em_flow_liveness(&graph,t,&live,10000));
        compare(&live,n,generated,ne,t);
    }
    em_flow_graph_free(&graph);
    em_flow_live_free(&live);
    assert(!graph.valid && !graph.storage && !graph.capacity);
    assert(!live.valid && !live.in && !live.out && !live.queue && !live.queued && !live.capacity);
    return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default=llvm('clang'))
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--source-root', type=Path, default=Path(R))
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='vcc-flow-') as temp:
        source, binary = Path(temp) / 'flow.c', Path(temp) / 'flow'
        source.write_text(SOURCE)
        flags = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if args.sanitize else []
        subprocess.run([args.cc, '-std=gnu11', '-O1', '-g', '-I', str(args.source_root / 'ccs'),
                        *flags, str(source), '-o', str(binary)], check=True, timeout=30)
        subprocess.run([str(binary)], check=True, timeout=10)
    print('shared bounded flow contracts: PASS')


if __name__ == '__main__':
    main()
