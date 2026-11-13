#!/usr/bin/env python3
"""Check unsigned32/64 reciprocal construction against division and remainder."""
import argparse
from pathlib import Path
import subprocess
import tempfile

from hostenv import R, llvm

SOURCE = r'''
#define VCC_UNITY
#include <assert.h>
#include "codegen/opt/udivconst.c"
#include "arch/x64/codegen/x64impl.h"
#include "codegen/walk/division-unsigned-constant.c"
#include "arch/a64/codegen/udivconst.c"
VPRIV int x64_nfree(codegen_context *e) { return 8 - __builtin_popcount(e->x64_context->live & X_TEMPS); }
/* The typed wrapper is unused in this arithmetic-only contract. */
VPRIV i128 em_trunc_to(cc *c, i128 v, ast_qualified_type t) { (void)c; (void)t; return v; }
VPRIV void slot_tag(codegen_context *e, a64_record *i, int64_t off, int64_t len)
{ (void)e; (void)i; (void)off; (void)len; }

static uint32_t apply(uint32_t n, const em_udivconst_recipe *r)
{
    if (r->compare) return n >= r->divisor;
    uint32_t q = (uint32_t)(((uint64_t)n * r->magic) >> 32);
    if (r->add) q += (n - q) >> 1;
    return q >> r->shift;
}
static uint32_t seed = 0x13579bdf;
static uint32_t random32(void) { seed ^= seed<<13; seed ^= seed>>17; seed ^= seed<<5; return seed; }
static void check(uint32_t n, uint32_t d, const em_udivconst_recipe *r)
{
    uint32_t q = apply(n,r);
    assert(q == n/d);
    assert(n-q*d == n%d);
}
static void divisor(uint32_t d, bool exhaustive)
{
    em_udivconst_recipe r;
    bool accepted = em_udivconst32(d,false,&r);
    assert(accepted == (d != 0 && (d & (d-1)) != 0));
    if (!accepted) return;
    assert(r.divisor==d && !r.remainder);
    em_udivconst_recipe rem;
    assert(em_udivconst32(d,true,&rem) && rem.remainder);
    assert(rem.magic==r.magic && rem.shift==r.shift && rem.add==r.add && rem.compare==r.compare);
    const uint32_t edge[] = {0,1,2,d-1,d,d+1,0x7fffffff,0x80000000,0xfffffffe,0xffffffff};
    for (unsigned i=0;i<sizeof edge/sizeof *edge;i++) check(edge[i],d,&r);
    for (unsigned i=0;i<64;i++) {
        uint32_t n=random32(); check(n,d,&r);
        uint32_t q=n/d;
        uint32_t multiple=q*d;
        check(multiple,d,&r);
        if (multiple) check(multiple-1,d,&r);
        if (multiple!=UINT32_MAX) check(multiple+1,d,&r);
    }
    if (exhaustive) for (uint32_t n=0;n<65536;n++) check(n,d,&r);
}
static uint64_t random64(void) { return ((uint64_t)random32()<<32) | random32(); }
static void check64(uint64_t n, uint64_t d, const em_udivconst_recipe *r)
{
    uint64_t q;
    if (r->compare) q=n>=d;
    else {
        q=(uint64_t)(((u128)n*r->magic)>>64);
        if (r->add) q += (n-q)>>1;
        q >>= r->shift;
    }
    assert(q==n/d);
    assert(n-q*d==n%d);
}
static void divisor64(uint64_t d)
{
    em_udivconst_recipe r,rem;
    bool accepted=em_udivconst64(d,false,&r);
    assert(accepted==(d!=0 && (d&(d-1))!=0));
    if (!accepted) return;
    assert(r.bits==64 && r.divisor==d && !r.remainder);
    assert(em_udivconst64(d,true,&rem) && rem.remainder);
    assert(rem.magic==r.magic && rem.shift==r.shift && rem.add==r.add && rem.compare==r.compare);
    uint64_t edge[]={0,1,2,d-1,d,d+1,INT64_MAX,UINT64_C(1)<<63,UINT64_MAX-1,UINT64_MAX};
    for (unsigned i=0;i<sizeof edge/sizeof *edge;i++) check64(edge[i],d,&r);
    for (unsigned i=0;i<64;i++) {
        uint64_t n=random64();check64(n,d,&r);
        uint64_t multiple=(n/d)*d;
        check64(multiple,d,&r);
        if (multiple) check64(multiple-1,d,&r);
        if (multiple!=UINT64_MAX) check64(multiple+1,d,&r);
    }
}
static void pressure(void)
{
    codegen_unit emit={0};
    xst x={0};
    codegen_context e={.unit=&emit,.x64_context=&x};
    const int regs[]={RAX,RCX,RDX,RSI,RDI,R8,R9,R10};
    for (unsigned count=0;count<=8;count++) {
        x.live=0;
        for (unsigned j=0;j<8-count;j++) x.live |= 1u<<regs[j];
        e.temporaries_in_use=(1u<<(CODEGEN_TEMPORARY_COUNT-count))-1;
        for (unsigned materialize=0;materialize<2;materialize++) {
            codegen_value a={.kind=materialize ? CODEGEN_VALUE_ADDRESS : CODEGEN_VALUE_REGISTER};
            for (unsigned add=0;add<2;add++) for (unsigned rem=0;rem<2;rem++) {
                em_udivconst_recipe r={.add=add,.remainder=rem};
                /* Independently model the peak: optional input materialization,
                 * a saved numerator when needed, and the large multiplier. */
                unsigned peak=materialize + (add || rem) + 1;
                assert(walk_division_unsigned_constant_check(&e,a,&r) == (count>=peak));
                assert(a64_udivconst_ok(&e,a,&r) == (count>=peak));
            }
        }
    }
    e.temporaries_in_use=0; e.temporaries_reserved=((1u<<CODEGEN_TEMPORARY_COUNT)-1) ^ 3u;
    em_udivconst_recipe corrected={.add=true};
    assert(a64_udivconst_ok(&e,(codegen_value){.kind=CODEGEN_VALUE_REGISTER},&corrected));
    assert(!a64_udivconst_ok(&e,(codegen_value){.kind=CODEGEN_VALUE_ADDRESS},&corrected));
}
int main(void)
{
    pressure();
    divisor(0,false); divisor(1,false);
    for (uint32_t d=2;d<=65;d++) divisor(d,true);
    for (unsigned shift=1;shift<32;shift++) {
        uint32_t n=UINT32_C(1)<<shift;
        divisor(n-1,false); divisor(n,false); divisor(n+1,false);
    }
    for (unsigned i=0;i<4096;i++) divisor(random32(),false);
    divisor(UINT32_MAX,false); divisor(UINT32_MAX-1,false);
    divisor64(0); divisor64(1);
    for (uint64_t d=2;d<=65;d++) divisor64(d);
    for (unsigned shift=1;shift<64;shift++) {
        uint64_t d=UINT64_C(1)<<shift;
        divisor64(d-1);divisor64(d);divisor64(d+1);
    }
    for (unsigned i=0;i<4096;i++) divisor64(random64());
    /* Full-width uniform samples mostly exercise very large divisors. Add
     * independent samples at each width to vary reciprocal/postshift classes. */
    for (unsigned bits=2;bits<64;bits++) for (unsigned i=0;i<32;i++) {
        uint64_t d=(random64() & ((UINT64_C(1)<<bits)-1)) | (UINT64_C(1)<<(bits-1));
        divisor64(d);
    }
    divisor64(UINT64_MAX);divisor64(UINT64_MAX-1);
    em_udivconst_recipe r;
    assert(em_udivconst64(3,false,&r) && r.magic==UINT64_C(0xaaaaaaaaaaaaaaab) && r.shift==1 && !r.add);
    assert(em_udivconst64(7,false,&r) && r.magic==UINT64_C(0x2492492492492493) && r.shift==2 && r.add);
    assert(em_udivconst64(10,false,&r) && r.magic==UINT64_C(0xcccccccccccccccd) && r.shift==3 && !r.add);
    assert(em_udivconst32(3,false,&r) && r.magic==UINT32_C(0xaaaaaaab) && r.shift==1 && !r.add);
    assert(em_udivconst32(7,false,&r) && r.magic==UINT32_C(0x24924925) && r.shift==2 && r.add);
    assert(em_udivconst32(10,false,&r) && r.magic==UINT32_C(0xcccccccd) && r.shift==3 && !r.add);
    return 0;
}
'''




def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default=llvm('clang'))
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='vcc-udivconst-') as tmp:
        source = Path(tmp) / 'effects.c'
        binary = Path(tmp) / 'effects'
        source.write_text(SOURCE)
        flags = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if args.sanitize else []
        subprocess.run([args.cc, '-std=gnu11', '-O1', '-g', '-Wno-undefined-internal', '-I', str(Path(R) / 'src'), '-I', str(Path(R) / 'src' / 'include'),
                        *flags, str(source), '-o', str(binary)], check=True, timeout=30)
        subprocess.run([str(binary)], check=True, timeout=10)
    print('unsigned32/64 reciprocal arithmetic contracts: PASS')


if __name__ == '__main__':
    main()
