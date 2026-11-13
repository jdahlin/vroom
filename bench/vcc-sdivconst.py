#!/usr/bin/env python3
"""Check signed32/64 reciprocal arithmetic and native temporary budgets."""
import argparse
from pathlib import Path
import subprocess
import tempfile

from hostenv import R, llvm

SOURCE = r'''
#define VCC_UNITY
#include <assert.h>
#include "codegen/opt/sdivconst.c"
#include "arch/x64/codegen/x64-internal.h"
#include "codegen/walk/division-signed-constant.c"
#include "arch/a64/codegen/sdivconst.c"
VPRIV int x64_nfree(codegen_context *e) { return 8 - __builtin_popcount(e->x64_context->live & X_TEMPS); }
VPRIV i128 em_trunc_to(cc *c, i128 v, ast_qualified_type t) { (void)c; (void)t; return v; }
VPRIV void slot_tag(codegen_context *e, ins *i, int64_t off, int64_t len)
{ (void)e; (void)i; (void)off; (void)len; }

static uint64_t seed=UINT64_C(0x987654321abcdeff);
static uint64_t random64(void) { seed^=seed<<13;seed^=seed>>7;seed^=seed<<17;return seed; }
static int64_t apply(int64_t n, const em_sdivconst_recipe *r)
{
    int64_t q;
    if (r->bits==32) {
        q=((int64_t)(int32_t)n * (int32_t)r->magic)>>32;
        if (r->add) q=(int32_t)((uint32_t)q+(uint32_t)n);
        q >>= r->shift;
        q += (uint32_t)q>>31;
    } else {
        q=(int64_t)(((i128)n*r->magic)>>64);
        if (r->add) q=(int64_t)((uint64_t)q+(uint64_t)n);
        q >>= r->shift;
        q += (uint64_t)q>>63;
    }
    return q;
}
static void check(int64_t n, int64_t d, const em_sdivconst_recipe *r)
{
    int64_t q=apply(n,r);
    int64_t signedq=r->negative ? -q : q;
    assert(signedq==n/d);
    assert((i128)n-(i128)q*r->divisor==n%d);
}
static void divisor(int64_t d, unsigned bits)
{
    em_sdivconst_recipe r,rem;
    uint64_t mag=(uint64_t)(d<0 ? -(i128)d : d);
    bool accepted=bits==32 ? em_sdivconst32((int32_t)d,false,&r) : em_sdivconst64(d,false,&r);
    assert(accepted==(mag!=0 && (mag&(mag-1))!=0));
    if (!accepted) return;
    assert(r.bits==bits && r.divisor==mag && r.negative==(d<0) && !r.remainder);
    assert(bits==32 ? em_sdivconst32((int32_t)d,true,&rem) : em_sdivconst64(d,true,&rem));
    assert(rem.remainder && rem.magic==r.magic && rem.shift==r.shift && rem.add==r.add);
    int64_t minimum=bits==32 ? INT32_MIN : INT64_MIN;
    int64_t maximum=bits==32 ? INT32_MAX : INT64_MAX;
    int64_t edge[]={minimum,minimum+1,maximum,maximum-1,0,1,-1,d,-d};
    for (unsigned j=0;j<sizeof edge/sizeof *edge;j++) check(edge[j],d,&r);
    for (unsigned j=0;j<64;j++) {
        int64_t n=bits==32 ? (int32_t)random64() : (int64_t)random64();
        check(n,d,&r);
        int64_t multiple=(n/d)*d;
        check(multiple,d,&r);
        if (multiple>minimum) check(multiple-1,d,&r);
        if (multiple<maximum) check(multiple+1,d,&r);
    }
}
static void pressure(void)
{
    codegen_unit emit={0}; xst x={0}; codegen_context e={.unit=&emit,.x64_context=&x};
    const int regs[]={RAX,RCX,RDX,RSI,RDI,R8,R9,R10};
    for (unsigned count=0;count<=8;count++) {
        x.live=0;
        for (unsigned j=0;j<8-count;j++) x.live|=1u<<regs[j];
        e.temporaries_in_use=(1u<<(CODEGEN_TEMPORARY_COUNT-count))-1;
        for (unsigned materialize=0;materialize<2;materialize++)
        for (unsigned add=0;add<2;add++) for (unsigned rem=0;rem<2;rem++) {
            em_sdivconst_recipe r={.add=add,.remainder=rem};
            codegen_value a={.kind=materialize ? CODEGEN_VALUE_ADDRESS : CODEGEN_VALUE_REGISTER};
            unsigned needed=materialize+1+(add||rem);
            assert(walk_division_signed_constant_check(&e,a,&r)==(count>=needed));
            assert(a64_sdivconst_ok(&e,a,&r)==(count>=needed));
        }
    }
}
int main(void)
{
    pressure();
    for (unsigned bits=32;bits<=64;bits+=32) {
        divisor(0,bits);divisor(1,bits);divisor(-1,bits);
        for (int64_t d=2;d<=65;d++) { divisor(d,bits);divisor(-d,bits); }
        for (unsigned shift=1;shift<bits-1;shift++) {
            int64_t d=INT64_C(1)<<shift;
            for (int delta=-1;delta<=1;delta++) {
                divisor(d+delta,bits);divisor(-(d+delta),bits);
            }
        }
        divisor(bits==32 ? INT32_MIN : INT64_MIN,bits);
        divisor(bits==32 ? INT32_MAX : INT64_MAX,bits);
        for (unsigned j=0;j<1024;j++)
            divisor(bits==32 ? (int32_t)random64() : (int64_t)random64(),bits);
    }
    em_sdivconst_recipe r;
    assert(em_sdivconst32(3,false,&r) && r.magic==INT32_C(0x55555556) && r.shift==0 && !r.add);
    assert(em_sdivconst32(6,false,&r) && r.magic==INT32_C(0x55555556) && r.shift==1 && !r.add);
    assert(em_sdivconst64(3,false,&r) && r.magic==INT64_C(0x5555555555555556) && r.shift==0 && !r.add);
    assert(em_sdivconst64(6,false,&r) && r.magic==INT64_C(0x5555555555555556) && r.shift==1 && !r.add);
    assert(em_sdivconst64(9,false,&r) && r.magic==INT64_C(0x71c71c71c71c71c8) && r.shift==2 && !r.add);
    assert(em_sdivconst32(7,false,&r) && r.magic==-INT32_C(1840700269) && r.shift==2 && r.add);
    assert(em_sdivconst64(7,false,&r) && r.magic==INT64_C(0x4924924924924925) && r.shift==1 && !r.add);
    return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default=llvm('clang'))
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='vcc-sdivconst-') as tmp:
        source = Path(tmp) / 'contract.c'
        binary = Path(tmp) / 'contract'
        source.write_text(SOURCE)
        flags = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if args.sanitize else []
        subprocess.run([args.cc, '-std=gnu11', '-O1', '-g', '-Wno-undefined-internal',
                        '-I', str(Path(R) / 'src'), '-I', str(Path(R) / 'src' / 'include'), *flags, str(source), '-o', str(binary)],
                       check=True, timeout=30)
        subprocess.run([str(binary)], check=True, timeout=10)
    print('signed32/64 reciprocal arithmetic and pressure contracts: PASS')


if __name__ == '__main__':
    main()
