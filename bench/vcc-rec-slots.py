#!/usr/bin/env python3
"""Exact-cell/overlap/escape contracts, including a deterministic interval oracle."""
import argparse
from pathlib import Path
import subprocess
import tempfile

from hostenv import R, llvm

SOURCE = r'''
#include <assert.h>
#include "codegen/rec/slots.c"
typedef struct { uint32_t id; int64_t off; uint32_t size; } test_access;
static uint32_t seed = 0x11223344;
static unsigned next(void) { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; }
static bool intersects(test_access a, test_access b) { return a.off < b.off+b.size && b.off < a.off+a.size; }
static void check(em_slot_cells *s, test_access *a, unsigned n, unsigned escaped)
{
    for (unsigned single=0;single<2;single++) {
    assert(em_slot_begin(s, 9));
    for (unsigned k=0;k<n;k++) {
        if (single) em_slot_collect(s,a[k].id,a[k].off,a[k].size);
        else em_slot_access(s,a[k].id,a[k].off,a[k].size);
    }
    for (unsigned id=1;id<9;id++) if (escaped & (1u<<id)) em_slot_escape(s,id);
    if (!single) for (unsigned k=0;k<n;k++) em_slot_detail(s,a[k].id,a[k].off,a[k].size);
    assert(em_slot_finish(s));
    for (unsigned k=0;k<n;k++) {
        if (!a[k].id) continue;
        bool bad=false;
        for (unsigned j=0;j<n;j++) {
            bool different=a[j].off!=a[k].off || a[j].size!=a[k].size;
            if ((!a[j].id || (a[j].id==a[k].id && different)) && intersects(a[k],a[j])) bad=true;
        }
        unsigned cell=em_slot_find(s,a[k].id,a[k].off,a[k].size);
        assert(cell && cell<s->count);
        assert(!!(s->flags[cell]&EM_SLOT_BAD)==bad);
        assert(!!(s->flags[cell]&EM_SLOT_ESCAPED)==!!(escaped&(1u<<a[k].id)));
        for (unsigned j=0;j<k;j++) if (a[j].id && a[j].id!=a[k].id)
            assert(cell!=em_slot_find(s,a[j].id,a[j].off,a[j].size));
    }
    }
}
int main(void)
{
    em_slot_cells s={0};
    /* Offset reuse is not allocation identity. Adjacent cells do not overlap.
     * Width changes and containment invalidate all intersecting exact cells.
     * An observable (ID 0) test_access poisons even a cell with no surviving load.
     */
    /* Both tiny-sort and general-sort sides must preserve classifications,
     * especially duplicates and poison boundaries around the cutoff. */
    for (unsigned n=15;n<=17;n++) {
        test_access edges[20];
        for (unsigned k=0;k<n;k++)
            edges[k]=(test_access){1,(int64_t)(n-k)*8,4};
        edges[n]=(test_access){0,8,1};
        check(&s,edges,n+1,0);
    }
    test_access explicit[]={ {1,-16,8},{1,-16,8},{2,-16,8},{3,0,8},{3,8,8},
                        {4,0,8},{4,2,2},{4,7,2},{5,32,8},{0,39,1},
                        {6,64,8},{0,72,1},{7,80,8},{8,88,8} };
    check(&s,explicit,sizeof(explicit)/sizeof(*explicit),(1u<<7)|(1u<<8));
    /* Exact duplicate poison ranges may coalesce; same-offset wider ranges
     * must still poison cells beyond the earlier narrow range. */
    test_access poison[]={ {1,4,4},{2,8,4},{0,0,1},{0,0,1},
                           {0,0,8},{0,0,8},{0,8,1},{0,8,1} };
    check(&s,poison,sizeof(poison)/sizeof(*poison),0);
    assert(s.nw == 3);
    check(&s,explicit,sizeof(explicit)/sizeof(*explicit),(1u<<7)|(1u<<8));
    assert(em_slot_find(&s,3,16,8)==0);
    assert(em_slot_find(&s,2,-16,4)==0);
    assert(em_slot_find(&s,99,0,8)==0);
    /* Randomized, bounded oracle also exercises capacity reuse and changes
     * between one-range and multi-range allocations over repeated functions.
     */
    for (unsigned trial=0;trial<400;trial++) {
        test_access a[48];
        a[0]=(test_access){1,0,8};
        for (unsigned k=1;k<48;k++)
            a[k]=(test_access){next()%9,(int64_t)(next()%96)-48,1u<<(next()%4)};
        /* Include duplicate poison runs in the randomized interval oracle. */
        if (!(trial % 3)) {
            a[1]=(test_access){0,(int64_t)(next()%96)-48,1u<<(next()%4)};
            a[2]=a[3]=a[1];
        }
        check(&s,a,48,next()&0x1fe);
    }
    assert(em_slot_begin(&s,2));
    assert(!em_slot_finish(&s));
    em_slot_access(&s,0,0,8);
    assert(!em_slot_finish(&s));
    assert(em_slot_begin(&s,2));
    em_slot_access(&s,1,INT64_MAX,8);
    assert(!em_slot_finish(&s));
    assert(em_slot_begin(&s,2));
    em_slot_access(&s,2,0,8);
    assert(!em_slot_finish(&s));
    assert(em_slot_begin(&s,2));
    em_slot_access(&s,1,0,8);
    em_slot_access(&s,1,8,8);
    em_slot_detail(&s,1,INT64_MAX,8);
    assert(!em_slot_finish(&s));
    assert(!em_slot_begin(&s,0));
    assert(!em_slot_begin(&s,0x10001));
    assert(em_slot_begin(&s,2));
    em_slot_collect(&s,2,0,8); /* invalid ID must not index a[2] */
    assert(!s.valid && !em_slot_finish(&s));
    assert(em_slot_begin(&s,2));
    em_slot_collect(&s,1,INT64_MAX,8);
    assert(!s.valid);
    assert(em_slot_begin(&s,2));
    em_slot_collect(&s,1,0,0);
    assert(!s.valid);
    assert(em_slot_begin(&s,2));
    em_slot_collect(&s,0,INT64_MAX,1);
    assert(!s.valid);
    assert(em_slot_begin(&s,2));
    em_slot_collect(&s,1,-8,8);
    em_slot_collect(&s,1,-8,8); /* duplicates before the transition */
    em_slot_collect(&s,1,0,8);
    em_slot_collect(&s,1,-8,8); /* first range repeated after transition */
    assert(em_slot_finish(&s));
    assert(em_slot_find(&s,1,-8,8) && em_slot_find(&s,1,0,8));
    free(s.a); free(s.c); free(s.w); free(s.wend); free(s.flags);
    return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default=llvm('clang'))
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--check-mutation', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='vcc-rec-slots-') as tmp:
        source = Path(tmp) / 'slots.c'
        binary = Path(tmp) / 'slots'
        source.write_text(SOURCE)
        flags = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if args.sanitize else []
        subprocess.run([args.cc, '-std=gnu11', '-O1', '-g', '-I', str(Path(R) / 'src'), '-I', str(Path(R) / 'src' / 'include'),
                        *flags, str(source), '-o', str(binary)], check=True, timeout=30)
        subprocess.run([str(binary)], check=True, timeout=10)
        if args.check_mutation:
            implementation=(Path(R)/'src/codegen/rec/slots.c').read_text()
            implementation=implementation.replace('#include "../codegen-internal.h"',
                '#include "'+str(Path(R)/'src/codegen/codegen-internal.h')+'"')
            mutations = [
                ('streaming first-range', implementation.replace('if (before == 1)', 'if (false)')),
                ('poison width', implementation.replace(' && s->w[s->nw - 1].size == size', '')),
            ]
            for name, dropped in mutations:
                assert dropped != implementation
                source.write_text(SOURCE.replace('#include "codegen/rec/slots.c"',dropped))
                subprocess.run([args.cc,'-std=gnu11','-O1','-g','-I',str(Path(R)/'src'),'-I',str(Path(R)/'src'/'include'),
                                *flags,str(source),'-o',str(binary)],check=True,timeout=30)
                result=subprocess.run([str(binary)],capture_output=True,cwd=tmp,timeout=10)
                if result.returncode==0 or b'Assertion' not in result.stderr:
                    raise RuntimeError(name + ' mutation was not rejected')
    print('machine record exact-slot contracts: PASS')


if __name__ == '__main__':
    main()
