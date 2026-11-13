#!/usr/bin/env python3
"""Check signed/unsigned high64 lowering and temporary/implicit register aliases.

This host-only test needs a C compiler and the checkout, no target toolchain.
Use --sanitize to run the same contract checks with ASan and UBSan.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

from hostenv import R, llvm

SOURCE = r'''
#include <assert.h>
#include "arch/x64/codegen/mulhi.c"
static uint64_t regs[16], snapshot[16];
static uint32_t mask;
static int expected_op;
/* A small interpreter for the lowering interface, not x86 assembly text.
 * save/restore have their existing interface semantics; register moves and
 * the MULW/SMULW instructions execute immediately in this fixture. */
VPRIV int x64_toreg(ecx *e,val *v,int size)
{ (void)e;assert(size==8 && v->k==V_REG);return v->r; }
VPRIV void x64_tfree(ecx *e,int r) { e->x64->live &= ~(1u<<r); }
VPRIV void x64_mov(ecx *e,int rd,int rs) { (void)e;regs[rd]=regs[rs]; }
VPRIV rec *x64_op3(ecx *e,int op,int sz,int rd,int rs,int rt)
{
    (void)e;(void)rt;static rec record;
    assert(op==expected_op && sz==8 && rd==XNOREG);
    u128 product=op==X_SMULW ? (u128)((i128)(int64_t)regs[RAX]*(i128)(int64_t)regs[rs])
                                : (u128)regs[RAX]*regs[rs];
    regs[RAX]=(uint64_t)product;regs[RDX]=(uint64_t)(product>>64);
    return &record;
}
VPRIV uint32_t x64_save(ecx *e,const uint8_t *fixed,int n,int k0,int k1)
{
    assert(n==2 && fixed[0]==RAX && fixed[1]==RDX);
    mask=e->x64->live & ((1u<<RAX)|(1u<<RDX));
    mask &= ~((1u<<k0)|(1u<<k1));
    for(unsigned r=0;r<16;r++) if(mask&(1u<<r)) snapshot[r]=regs[r];
    e->x64->pushes+=__builtin_popcount(mask);return mask;
}
VPRIV void x64_restore(ecx *e,uint32_t saved)
{
    assert(saved==mask);
    for(unsigned r=0;r<16;r++)if(mask&(1u<<r))regs[r]=snapshot[r];
    e->x64->pushes-=__builtin_popcount(mask);
}
int main(void)
{
    const uint8_t pool[]={RAX,RCX,RDX,RSI,RDI,R8,R9,R10};
    const uint64_t values[]={0,1,UINT64_MAX,UINT64_MAX-1,UINT64_C(0x8000000000000000),
                            UINT64_C(0x13579bdf2468ace0),UINT64_C(0x7fffffffffffffff),3};
    const unsigned nv=sizeof(values)/sizeof(values[0]);
    for(unsigned signed_product=0;signed_product<2;signed_product++)
    for(unsigned a=0;a<8;a++)for(unsigned b=0;b<8;b++)
    for(unsigned live=0;live<4;live++)for(unsigned v=0;v<nv;v++) {
        xst x={.pushes=7};ecx e={.x64=&x};
        x.live=X_TEMPS & ~((1u<<RAX)|(1u<<RDX));
        if(live&1)x.live|=1u<<RAX;
        if(live&2)x.live|=1u<<RDX;
        unsigned ra=pool[a],rb=pool[b];x.live|=(1u<<ra)|(1u<<rb);
        uint32_t before_live=x.live;
        for(unsigned r=0;r<16;r++)regs[r]=UINT64_C(0x1234567800000000)+r;
        regs[ra]=values[v];regs[rb]=values[(v+3)%nv];
        uint64_t before[16];memcpy(before,regs,sizeof before);
        u128 product=signed_product ? (u128)((i128)(int64_t)regs[ra]*(i128)(int64_t)regs[rb])
                                    : (u128)regs[ra]*regs[rb];
        uint64_t expected=(uint64_t)(product>>64);
        expected_op=signed_product ? X_SMULW : X_MULW;
        val va={.k=V_REG,.r=ra},vb={.k=V_REG,.r=rb};
        val result=signed_product ? x64_smulhi(&e,va,vb) : x64_umulhi(&e,va,vb);
        assert(result.k==V_REG && result.r==ra && regs[ra]==expected);
        assert(x.pushes==7);
        assert(x.live==((before_live & ~(1u<<rb)) | (1u<<ra)));
        for(unsigned r=0;r<16;r++)
            if(r!=ra && r!=rb && r!=R11 && (before_live&(1u<<r)))
                assert(regs[r]==before[r]);
    }
    return 0;
}
'''



def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default=llvm('clang'))
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='vcc-mulhi-') as tmp:
        source = Path(tmp) / 'effects.c'
        binary = Path(tmp) / 'effects'
        source.write_text(SOURCE)
        flags = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if args.sanitize else []
        subprocess.run([args.cc, '-std=gnu11', '-O1', '-g', '-I', str(Path(R) / 'src'),
                        *flags, str(source), '-o', str(binary)], check=True, timeout=30)
        subprocess.run([str(binary)], check=True, timeout=10)
    print('x86 high-product register contracts: PASS')


if __name__ == '__main__':
    main()
