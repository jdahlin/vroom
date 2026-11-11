#!/usr/bin/env python3
"""Check native integer overload opcodes and actual LLVM reduction semantics."""
import argparse
import importlib.util
import json
from pathlib import Path
import re
import subprocess
spec=importlib.util.spec_from_file_location('probes',Path(__file__).with_name('probe-x86-intrinsic-data.py'))
p=importlib.util.module_from_spec(spec);spec.loader.exec_module(p)
ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--output',type=Path,required=True);ap.add_argument('--clang',default='clang');args=ap.parse_args()
out=args.output.resolve();out.mkdir(parents=True,exist_ok=True);records=[]
for row in json.loads((p.gen.DATA/'elementwise.json').read_text()):
    types=row['sig'].split();decl=[]
    for token in sorted(set(types)):
        if token.startswith('V'):
            m=re.fullmatch(r'V(\d+)(\w)',token);n=int(m[1]);scalar=m[2]
            decl.append(f'typedef {p.SCALAR[scalar]} {token} __attribute__((vector_size({n*p.gen.BITS[scalar]//8})));')
    builtin='__builtin_reduce_add' if row['op']=='REDUCE_ADD' else '__builtin_elementwise_'+row['op'].lower()
    source='\n'.join(decl)+'\n'+p.ctype(types[0])+' probe('+','.join(p.ctype(t)+' a'+str(i) for i,t in enumerate(types[1:]))+') { return '+builtin+'('+','.join('a'+str(i) for i in range(len(types)-1))+'); }\n'
    name=row['op']+'-'+types[1];path=out/(name+'.c');path.write_text(source)
    for mode in ['native']+(['ir'] if row['op']=='REDUCE_ADD' else []):
        dest=out/(name+('.ll' if mode=='ir' else '.s'));cmd=[args.clang,'--target=x86_64-linux-gnu','-O0' if mode=='ir' else '-O2','-S',str(path),'-o',str(dest)]+(['-emit-llvm'] if mode=='ir' else [])+['-m'+f for f in row['features'].split(',')]
        result=subprocess.run(cmd,capture_output=True,text=True,timeout=20);text=dest.read_text() if dest.exists() else ''
        okay=result.returncode==0
        if okay and mode=='ir':okay='@llvm.vector.reduce.add.' in text
        elif okay and row['op']!='REDUCE_ADD':okay=bool(re.search(r'^\s*'+row['mnemonic']+r'\s',text,re.M))
        records.append(dict(name=name,mode=mode,command=cmd,rc=result.returncode,passed=okay,stderr=result.stderr))
(out/'results.json').write_text(json.dumps(records,indent=2)+'\n');failed=[r for r in records if not r['passed']]
print(f'X9 elementwise probes: {len(records)-len(failed)}/{len(records)} pass')
for r in failed:print('FAIL',r['name'],r['mode'],r['stderr'])
raise SystemExit(bool(failed))
