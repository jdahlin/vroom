#!/usr/bin/env python3
"""Check x86 host CRC/lexer dispatch, including baseline CPUs with --qemu.

Uses the same compiler binary for every feature combination. --qemu requires
an x86-64 baseline build (the default), not a HOST_CPU=-march=native build.
"""
import argparse
import os
from pathlib import Path
import platform
import subprocess
import tempfile
import hostenv

ROOT = Path(__file__).resolve().parent.parent
PROBE = r'''
#include "port/hostcrc.h"
#include "common/intern.h"
#include <assert.h>
#include <stdio.h>
static unsigned ref(unsigned h, unsigned long long w, int n) {
    for (int j=0; j<n; j++,w>>=8) {
        h ^= (unsigned char)w;
        for (int k=0;k<8;k++) h=(h>>1)^((0u-(h&1))&0x82f63b78u);
    }
    return h;
}
int main(void) {
    unsigned h=~0u; const char *s="123456789";
    for (int i=0;i<9;i++) h=__crc32cb(h,s[i]);
    assert((h^~0u)==0xe3069283u);
    unsigned long long x=1;
    for (int i=0;i<10000;i++) {
        x=x*6364136223846793005ull+1;
        h=(unsigned)(x>>32);
        assert(__crc32cb(h,(unsigned char)x)==ref(h,x,1));
        assert(__crc32cd(h,x)==ref(h,x,8));
        assert(vcrc32c_two(h,x,~x)==ref(ref(h,x,8),~x,8));
    }
    char text[544]; for (int i=0;i<544;i++) text[i]=(char)(i*13);
    for (int n=0;n<=512;n++) printf("%08x\n",vintern_hash(text,n));
}
'''


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--bin-dir', default=os.environ.get('VCC_BIN', hostenv.BUILD))
    ap.add_argument('--qemu', action='store_true')
    args = ap.parse_args()
    if platform.machine() != 'x86_64':
        print('skip: x86 host dispatch')
        return
    bins = Path(args.bin_dir).resolve()
    with tempfile.TemporaryDirectory(prefix='vcc-host-dispatch-') as tmp:
        d = Path(tmp)
        (d/'crc.c').write_text(PROBE)
        subprocess.run([hostenv.llvm('clang'), '-O2', '-march=x86-64', '-D_GNU_SOURCE',
                        '-I'+str(ROOT/'src/include'), str(d/'crc.c'),
                        str(ROOT/'src/common/intern.c'), '-pthread', '-o', str(d/'crc')], check=True)
        src = d/'input.c'
        src.write_text(''.join('/*'+'x'*n+'*/\nconst char *s%d="%s\\\"";\n' % (n,'y'*n)
                               for n in (15,16,17,31,32,33,63,64,65,127,128,129)) +
                       'int f(int x) { return x * 7 + (x >> 2); }\n')
        prefixes = [('host', [])]
        if args.qemu:
            prefixes += [(cpu, ['qemu-x86_64', '-cpu', cpu]) for cpu in
                         ('Conroe', 'Nehalem', 'Haswell,-hle,-rtm', 'Haswell,-hle,-rtm,-xsave')]
        expected = {}
        for label, prefix in prefixes:
            for crc in ('software', 'auto'):
                for simd in ('scalar', 'avx2', 'auto'):
                    env = dict(os.environ, VCC_CRC=crc, VCC_LEX_SIMD=simd, VCC_VIR='0')
                    def run(command):
                        return subprocess.run(prefix+list(map(str, command)), env=env,
                                              capture_output=True, check=True, timeout=30).stdout
                    outputs = {'crc': run([d/'crc']),
                               'tokens': run([bins/'lextest', 'dump', src]),
                               'pp': run([bins/'vpp', '--target=x86_64-linux-gnu', '-E', '-P', src])}
                    run([bins/'vpp', '--target=x86_64-linux-gnu', '-c', src, '-o', d/'out.o'])
                    outputs['object'] = (d/'out.o').read_bytes()
                    for name, data in outputs.items():
                        if name not in expected:
                            expected[name] = data
                        assert data == expected[name], (label, crc, simd, name)
            print(f'{label}: all six CRC/lexer combinations identical')


if __name__ == '__main__':
    main()
