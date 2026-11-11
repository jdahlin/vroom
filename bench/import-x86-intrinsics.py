#!/usr/bin/env python3
"""Import pinned LLVM builtin records without handwritten prototype guesses."""
import json,pathlib,re,hashlib
import argparse, subprocess, tempfile, urllib.request
root=pathlib.Path(__file__).resolve().parent.parent
data=root/'ccs/vcc/arch/x64/intrinsics'
pinned=json.loads((data/'builtins.json').read_text())
ap=argparse.ArgumentParser(description='Refresh pinned LLVM X9 metadata; normal generation/checking is offline.')
ap.add_argument('--source-dir',type=pathlib.Path,required=True)
ap.add_argument('--headers',type=pathlib.Path,required=True)
ap.add_argument('--llvm-bin',type=pathlib.Path,required=True)
ap.add_argument('--fetch',action='store_true',help='Fetch missing pinned upstream files, then verify hashes')
args=ap.parse_args()
upstream=args.source_dir.resolve()
for entry in pinned['sources']:
 p=upstream/entry['path']
 if not p.exists() and args.fetch:
  p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(urllib.request.urlopen(entry['url']).read())
 if hashlib.sha256(p.read_bytes()).hexdigest()!=entry['sha256']:raise SystemExit('Source hash mismatch: '+str(p))
temporary=tempfile.TemporaryDirectory(prefix='x9-metadata-');r=pathlib.Path(temporary.name)
for stem,target in [('BuiltinsX86','builtins-x86.json'),('BuiltinsX86_64','builtins-x86-64.json')]:
 subprocess.run([str(args.llvm_bin/'llvm-tblgen'),'--dump-json','-I',str(upstream/'clang/include'),str(upstream/('clang/include/clang/Basic/'+stem+'.td')),'-o',str(r/target)],check=True)
h={}
for p in sorted(args.headers.glob('*.h')):
 for name in set(re.findall(r'\b__builtin_ia32_\w+',p.read_text())):h.setdefault(name,[]).append(p.name)
if len(h)!=pinned['header_inventory_count']:raise SystemExit('Header inventory differs from pinned version')
for name,digest in pinned['header_sources'].items():
 if hashlib.sha256((args.headers/name).read_bytes()).hexdigest()!=digest:raise SystemExit('Header hash mismatch: '+name)
rows={}

for filename in ['builtins-x86.json','builtins-x86-64.json']:
 d=json.loads((r/filename).read_text())
 for key,v in d.items():
  if not isinstance(v,dict) or 'X86Builtin' not in v.get('!superclasses',[]): continue
  name='__builtin_ia32_'+key
  attrs=[]
  for a in v['Attributes']:
   x=d[a['def']];attrs.append({k:z for k,z in x.items() if not k.startswith('!')})
  rows[name]=dict(name=name,prototype=v['Prototype'],features=v['Features'],attributes=attrs,headers=sorted(h.get(name,[])),source=('BuiltinsX86_64.td' if '64' in filename else 'BuiltinsX86.td'),ranges=[],special_validation=[])
s=(upstream/'clang/lib/Sema/SemaX86.cpp').read_text()
body=s[s.index('int i = 0, l = 0, u = 0;'):]
for block in body.split('break;'):
 names=re.findall(r'case X86::BI(__builtin_ia32_\w+):',block)
 values=[re.findall(r'\b'+var+r'\s*=\s*(\d+)\s*;',block) for var in ['i','l','u']]
 if names and all(values):
  i,l,u=[int(v[-1]) for v in values]
  for name in names:
   if name in rows:rows[name]['ranges'].append(dict(argument=i,min=l,max=u))
# Earlier helper checks cannot be reduced to one interval (rounding/SAE, scale, tiles).
for fname in ['CheckBuiltinRoundingOrSAE','CheckBuiltinGatherScatterScale','CheckBuiltinTileArguments']:
 start=s.index('SemaX86::'+fname);end=s.find('\nbool SemaX86::',start+1)
 block=s[start:end if end!=-1 else len(s)]
 for name in re.findall(r'case X86::BI(__builtin_ia32_\w+):',block):
  if name in rows:rows[name]['special_validation'].append(fname)
obj=dict(revision=pinned['revision'],license=pinned['license'],sources=pinned['sources'],header_sources=pinned['header_sources'],header_inventory_count=len(h),builtins=[rows[n] for n in sorted(rows)])
(data/'builtins.json').write_text(json.dumps(obj,indent=2)+'\n')
(data/'LICENSE.TXT').write_bytes((upstream/'LICENSE.TXT').read_bytes())
print('rows',len(rows),'rangevalidated',sum(bool(x['ranges']) for x in rows.values()),'special',sum(bool(x['special_validation']) for x in rows.values()))
