#!/usr/bin/env python3
"""Generate and check Clang type/constant/feature probes for reviewed X9 rows.
This is a development oracle, not a compiler runtime dependency. No generated
instruction is executed, so AVX512/XSAVE hardware is not required.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import re
import subprocess

spec=importlib.util.spec_from_file_location('gen',Path(__file__).with_name('gen-x86-intrinsics.py'))
gen=importlib.util.module_from_spec(spec);spec.loader.exec_module(gen)
SCALAR={'v':'void','b':'_Bool','c':'char','a':'signed char','C':'unsigned char','s':'short','S':'unsigned short','i':'int','I':'unsigned int','l':'long','L':'unsigned long','q':'long long','Q':'unsigned long long','f':'float','d':'double','h':'_Float16','j':'__bf16','z':'__SIZE_TYPE__','t':'__UINT64_TYPE__'}

def ctype(token):
    if token.startswith('P'):return ctype(token[1:])+' *'
    if token.startswith('K'):return 'const '+ctype(token[1:])
    return token if token.startswith('V') else SCALAR[token]

def source(name,row,mapping,nonconstant=None,immediate=None):
    sig,width=gen.signature(row['prototype']);types=sig.split();decl=[]
    for token in sorted(set(t.lstrip('!PK') for t in types)):
        if token.startswith('V'):
            match=re.fullmatch(r'V(\d+)(\w)',token);n=int(match[1]);scalar=match[2]
            decl.append(f'typedef {SCALAR[scalar]} {token} __attribute__((vector_size({n*gen.BITS[scalar]//8})));')
    params=[];args=[]
    for i,token in enumerate(types[1:]):
        if token.startswith('!') and i!=nonconstant:args.append(str(mapping['evidence']['probe_immediate'] if immediate is None else immediate))
        else:params.append(ctype(token.lstrip('!'))+' a'+str(i));args.append('a'+str(i))
    return '\n'.join(decl)+'\n'+ctype(types[0])+' probe('+','.join(params)+') { return '+name+'('+','.join(args)+'); }\n'

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--output',type=Path,required=True);ap.add_argument('--clang',default='clang');args=ap.parse_args()
    out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    rows={r['name']:r for r in json.loads((gen.DATA/'builtins.json').read_text())['builtins']};maps=json.loads((gen.DATA/'lowerings.json').read_text());records=[]
    def check(name,label,text,features,expected,opcode=None):
        p=out/(name+'-'+label+'.c');p.write_text(text);asm=p.with_suffix('.s')
        cmd=[args.clang,'--target=x86_64-linux-gnu','-O2','-S',str(p),'-o',str(asm)]+features
        result=subprocess.run(cmd,capture_output=True,text=True,timeout=20)
        (p.with_suffix('.stderr')).write_text(result.stderr)
        valid=(result.returncode==0) if expected else result.returncode>0 and 'error:' in result.stderr
        if opcode and valid:valid=bool(re.search(r'^\s*'+re.escape(opcode)+r'\s',asm.read_text(),re.M))
        records.append(dict(name=name,case=label,command=cmd,rc=result.returncode,expected_success=expected,passed=valid))
        return valid
    for name,mapping in maps.items():
        row=rows[name];flags=['-m'+f for f in row['features'].split(',') if f];text=source(name,row,mapping)
        check(name,'positive',text,flags,True,mapping['mnemonic'])
        sig,width=gen.signature(row['prototype'])
        for index,token in enumerate(sig.split()[1:]):
            if token.startswith('!'):check(name,'nonconstant'+str(index),source(name,row,mapping,nonconstant=index),flags,False)
        # Exact feature requirements: remove each independently required feature
        # from the positive command. The source has no target attribute to hide it.
        for feature in row['features'].split(','):
            if feature:check(name,'missing-'+feature,text,flags+['-mno-'+feature],False)
    (out/'results.json').write_text(json.dumps(records,indent=2)+'\n')
    failed=[r for r in records if not r['passed']]
    print(f'X9 Clang probes: {len(records)-len(failed)}/{len(records)} pass, {len(maps)} reviewed builtin mappings')
    for row in failed:print('FAIL',row['name'],row['case'],row['rc'])
    raise SystemExit(bool(failed))
if __name__=='__main__':main()
