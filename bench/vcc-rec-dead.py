#!/usr/bin/env python3
"""Host contracts for x86 dead results and bounded dataflow fallback."""
import argparse
from pathlib import Path
import subprocess
import tempfile

from hostenv import R, llvm

SOURCE = r'''
#include <assert.h>
#include "arch/x64/emit/effects.c"
#include "emit/flow/graph.c"
#include "emit/flow/maskflow.c"
#include "arch/x64/emit/dead.c"
#define OP(o,s,d,a,b) ((rec){.op=o,.sz=s,.r={d,a,b,XNOREG}})
#define IMM(r) OP(X_MOVI,8,r,XNOREG,XNOREG)
#define MOV(d,a) OP(X_MOV,8,d,a,XNOREG)
#define LABEL(n) ((rec){.op=X_LABEL,.label=n})
#define JUMP(n) ((rec){.op=X_JMP,.label=n})
#define BRANCH(n) ((rec){.op=X_JCC,.label=n})
static struct { uint32_t old, next; } remaps[1024];
static unsigned nremaps;
void dbg_remap(ecx *e, uint32_t *cursor, uint32_t old, uint32_t next)
{
    (void)e; (void)cursor;
    assert(nremaps < 1024);
    remaps[nremaps].old=old;
    remaps[nremaps++].next=next;
}
static uint32_t optimize(rec *records, uint32_t count, uint32_t budget, bool debug, int interrupt)
{
    wkst w={.falign=16};
    xst x={.in=records,.nin=count,.interrupt=interrupt};
    ecx e={.wk=&w,.x64=&x,.ndl=debug};
    nremaps=0;
    x64_dead_budget(&e,budget);
    return x.nin;
}
static void unchanged(rec *a, uint32_t n, uint32_t budget)
{
    rec before[300];
    assert(n<=300);
    memcpy(before,a,n*sizeof(rec));
    assert(optimize(a,n,budget,false,0)==n);
    assert(!memcmp(a,before,n*sizeof(rec)));
}
int main(void)
{
    rec dead[]={IMM(R10),IMM(RAX)};
    assert(optimize(dead,2,UINT32_MAX,true,0)==1 && dead[0].r[0]==RAX);
    assert(nremaps==3 && remaps[0].old==0 && remaps[0].next==0 &&
           remaps[1].old==1 && remaps[1].next==0 &&
           remaps[2].old==UINT32_MAX && remaps[2].next==1);
    /* Budget exhaustion is transactional even after an earlier successful
       analysis reused the same worker buffers. */
    rec exhausted[]={IMM(R10),LABEL(1),IMM(RAX)};
    unchanged(exhausted,3,0); unchanged(exhausted,3,1);
    /* Liveness must meet both successors, including a cyclic backedge. */
    rec diamond[]={IMM(R10),BRANCH(1),IMM(R10),JUMP(2),LABEL(1),IMM(R10),LABEL(2),MOV(RAX,R10)};
    assert(optimize(diamond,8,UINT32_MAX,false,0)==7 && diamond[0].op==X_JCC);
    rec loop[]={IMM(R10),LABEL(1),OP(X_ADDI,8,R10,R10,XNOREG),BRANCH(1),MOV(RAX,R10)};
    unchanged(loop,5,UINT32_MAX);
    /* Return registers and implicit printer epilogue inputs remain live. */
    const int exits[]={RAX,RDX,XMM0,XMM0+1,RSP,RBP,R12,RBX,R13,R14,R15};
    for(unsigned k=0;k<sizeof(exits)/sizeof(exits[0]);k++) {
        rec a[]={IMM(exits[k]),LABEL(1)};
        unchanged(a,2,UINT32_MAX);
    }
    /* A partial write reads the old high bits; a full overwrite can kill it. */
    rec partial[]={IMM(R10),OP(X_MOV,1,R10,RCX,XNOREG),MOV(RAX,R10)};
    unchanged(partial,3,UINT32_MAX);
    rec full[]={IMM(R10),OP(X_MOV,4,R10,RCX,XNOREG),MOV(RAX,R10)};
    assert(optimize(full,3,UINT32_MAX,false,0)==2 && full[0].sz==4);
    rec flags[]={OP(X_ADDI,8,R10,RDI,XNOREG),BRANCH(1),LABEL(1),IMM(RAX)};
    unchanged(flags,4,UINT32_MAX);
    /* Carry is an input to ADC, even if another ordinary result is dead. */
    rec carry[]={OP(X_ADDI,8,R10,RDI,XNOREG),OP(X_ADC,8,RAX,RAX,RCX),LABEL(1)};
    unchanged(carry,3,UINT32_MAX);
    rec callargs[]={IMM(RDI),OP(X_CALL,8,XNOREG,XNOREG,XNOREG),IMM(RAX)};
    unchanged(callargs,3,UINT32_MAX);
    const int observable[]={X_LD,X_IDIV,X_FSQRT,X_XADD,X_X87,X_BARRIER};
    for(unsigned k=0;k<sizeof(observable)/sizeof(observable[0]);k++) {
        rec a[]={OP(observable[k],8,R10,RDI,RCX),IMM(RAX)};
        unchanged(a,2,UINT32_MAX);
    }
    /* Extra entries, indirect edges, missing/duplicate labels and unknown
       opcodes invalidate the whole function, not just an isolated block. */
    const int opaque[]={X_RAW,X_NAMED,X_JMPR,X_LEAL,X_NOPS,255};
    for(unsigned k=0;k<sizeof(opaque)/sizeof(opaque[0]);k++) {
        rec a[]={IMM(R10),OP(opaque[k],8,XNOREG,XNOREG,XNOREG),IMM(RAX)};
        unchanged(a,3,UINT32_MAX);
    }
    rec missing[]={IMM(R10),JUMP(91),IMM(RAX)};
    unchanged(missing,3,UINT32_MAX);
    rec duplicate[]={IMM(R10),LABEL(1),LABEL(1),IMM(RAX)};
    unchanged(duplicate,4,UINT32_MAX);
    rec many[258]; many[0]=IMM(R10);
    for(unsigned k=1;k<258;k++) many[k]=LABEL(k);
    unchanged(many,258,UINT32_MAX);
    rec intr[]={IMM(R10),IMM(RAX)}, before[2];
    memcpy(before,intr,sizeof intr);
    assert(optimize(intr,2,UINT32_MAX,false,1)==2 && !memcmp(intr,before,sizeof intr));
    /* Ordinary functions without a lowering candidate must not allocate a
       graph. The explicit candidate enables the same real deletion. */
    em_flow_live_free(&x64_dead_work.live);
    em_flow_graph_free(&x64_dead_work.graph);
    rec gated[]={IMM(R10),IMM(RAX)};
    wkst w={.falign=16}; xst x={.in=gated,.nin=2};
    ecx e={.wk=&w,.x64=&x};
    x64_dead(&e);
    assert(x.nin==2 && !x64_dead_work.graph.storage && !x64_dead_work.live.in);
    e.discarded_scalar=true;
    x64_dead(&e);
    assert(x.nin==1 && gated[0].r[0]==RAX);
    /* An explicit noreturn convention cannot use the ordinary exit seed. */
    rec nonreturn[]={IMM(R10),IMM(RAX)};
    sym syms[2]={{0},{.flags=SF_NORETURN}}; cc c={.syms=syms};
    w.fsym=1; e.c=&c; x.in=nonreturn; x.nin=2;
    x64_dead(&e);
    assert(x.nin==2 && nonreturn[0].r[0]==R10);
    em_flow_live_free(&x64_dead_work.live);
    em_flow_graph_free(&x64_dead_work.graph);
    return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default=llvm('clang'))
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='vcc-rec-dead-') as tmp:
        source = Path(tmp) / 'dead.c'
        binary = Path(tmp) / 'dead'
        source.write_text(SOURCE)
        flags = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if args.sanitize else []
        subprocess.run([args.cc, '-std=gnu11', '-O1', '-g', '-I', str(Path(R) / 'src'),
                        *flags, str(source), '-o', str(binary)], check=True, timeout=30)
        subprocess.run([str(binary)], check=True, timeout=10)
    print('machine record dead-result contracts: PASS')


if __name__ == '__main__':
    main()
