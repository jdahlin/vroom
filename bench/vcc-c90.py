#!/usr/bin/env python3
"""C90/C94/C99 digraph and statement-scope contracts, including mixed-mode cache reuse."""
import argparse, json, os, pathlib, subprocess
from hostenv import BUILD, llvm
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--target', choices=['x86_64-linux-gnu', 'aarch64-linux-gnu'], default='x86_64-linux-gnu')
p.add_argument('--bin-dir', default=BUILD)
p.add_argument('--output', type=pathlib.Path, required=True)
a = p.parse_args()
out = a.output.resolve(); out.mkdir(parents=True, exist_ok=True)
fixture = pathlib.Path(__file__).resolve().parent.parent/'ccs/vcc/tests/c90'
vpp = str(pathlib.Path(a.bin_dir).resolve()/'vpp'); clang = llvm('clang')
modes = [('c89',0,0), ('c90',0,0), ('iso9899:1990',0,0), ('iso9899:199409',1,0), ('gnu89',1,0), ('gnu90',1,0), ('c99',1,1)]
records = []; jobs = []; references = {}
def run(cmd, *, expected=0, input=None):
    r = subprocess.run(list(map(str,cmd)), input=input, capture_output=True, text=True, timeout=60)
    records.append(dict(argv=list(map(str,cmd)), rc=r.returncode, stdout=r.stdout, stderr=r.stderr))
    if (expected == 0 and r.returncode) or (expected != 0 and not r.returncode):
        raise RuntimeError(str(cmd)+'\n'+r.stdout+r.stderr)
    return r
try:
    for mode, digraphs, c99 in modes:
        key = mode.replace(':','_')
        flags = ['--target='+a.target, '-std='+mode, '-pedantic-errors', '-DDIGRAPHS='+str(digraphs), '-DC99='+str(c99)]
        for name, cc in [('clang',clang), ('vcc',vpp)]:
            asm = out/(key+'-'+name+'.s'); exe = out/(key+'-'+name)
            run([cc,*flags,'-S',fixture/'modes.c','-o',asm])
            run([clang,'--target='+a.target,'-fuse-ld=lld',asm,'-o',exe])
            run((['qemu-aarch64','-L','/usr/aarch64-linux-gnu'] if a.target.startswith('aarch64') else [])+[exe])
            # Token pasting must use the same language-mode token boundaries.
            run([cc,*flags,'-E','-P',fixture/'paste.c'],expected=0 if digraphs else 1)
            # stdin and command-line macro bodies must obey the same mode as files.
            r = run([cc,*flags,'-E','-P','-x','c','-','-I',fixture,'-DCMD(x,y)=x %:%: y'],input='#include "modes.h"\nTEXT(CMD(1,2))\n')
            expected = '"12"' if digraphs else '"1 %:%: 2"'
            if expected not in r.stdout: raise RuntimeError('stdin/-D digraph mismatch: '+r.stdout)
            if name == 'vcc':
                references[key] = asm.read_bytes()
                for turn in range(3):
                    target = out/(f'batch-{turn}-'+key+'.s')
                    jobs.append((key,target,['--target='+a.target,'-std='+mode,'-pedantic-errors','-DDIGRAPHS='+str(digraphs),'-DC99='+str(c99),'-S',str(fixture/'modes.c'),'-o',str(target)]))
    # Same cached header, with different modes interleaved in a shared process.
    jobs.sort(key=lambda j:j[1].name)
    jf = out/'jobs'; jf.write_text('\n'.join('\x1f'.join(['-']+j[2]) for j in jobs)+'\n')
    run([vpp,'--batch',jf,'-j','4','-C',fixture])
    for key, target, _ in jobs:
        if target.read_bytes()!=references[key]: raise RuntimeError('mixed-mode cache mismatch: '+str(target))
    print('7 language modes: Clang/vcc runtime, paste, stdin/-D; 21 mixed-mode batch assemblies match')
finally:
    (out/'commands.json').write_text(json.dumps(records,indent=2)+'\n')
