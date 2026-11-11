#!/usr/bin/env python3
"""Immediate constraints and persisted descriptor identities for X9 runtime input."""
import argparse,pathlib,subprocess,os,json,re
p=argparse.ArgumentParser();p.add_argument('--runtime',type=pathlib.Path,required=True);p.add_argument('--vpp',required=True);p.add_argument('--output',type=pathlib.Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
rows=json.loads((a.runtime/'summary.json').read_text())['rows'];defs=(a.runtime/'wrappers.c').read_text().split('static volatile')[0];env={**os.environ,'TMPDIR':os.environ.get('TMPDIR', '/tmp')};flags=['-msse4.2','-mcrc32','-O2','-S'];records=[]
for row in rows:
 args=row['sig'][1:];ci=[i for i,t in enumerate(args) if t.startswith('!')]
 if not ci:continue
 j=ci[0];cases=[('runtime','v',False)]
 if row['constraints']:
  _,lo,hi=map(int,row['constraints'].split(';')[0].split(':'));cases += [('below',str(lo-1),False),('above',str(hi+1),False),('minimum',str(lo),True),('maximum',str(hi),True)]
 for case,value,want in cases:
  aa=[value if i==j else '('+t+'){0}' for i,t in enumerate(args)];src=a.output/(row['id']+'-'+case+'.c');src.write_text(defs+row['sig'][0]+' test(int v){return '+row['name']+'('+','.join(aa)+');}\n')
  got=[]
  for compiler in ['/usr/lib/llvm-23/bin/clang',a.vpp]:
   cmd=[compiler,*flags,str(src),'-o',str(src)+'.s'];q=subprocess.run(cmd,capture_output=True,text=True,env=env,timeout=30);got.append(q.returncode==0);records.append(dict(builtin=row['name'],case=case,expected=want,cmd=cmd,rc=q.returncode,stderr=q.stderr))
  if got!=[want,want]:print('MISMATCH',row['name'],case,want,got,flush=True)
(a.output/'diagnostics.json').write_text(json.dumps(records,indent=2)+'\n')
errors=[r for r in records if (r['rc']==0)!=r['expected']]
# Persist every lowered ID in shared declaration bodies; prove actual hits.
h=a.output/'common.h';h.write_text((a.runtime/'wrappers.c').read_text());jobs=[]
for i in range(4):
 src=a.output/('unit'+str(i)+'.c');src.write_text('#include "common.h"\nint unique'+str(i)+';\n');out=a.output/('unit'+str(i)+'.s');cmd=[*flags,str(src),'-o',str(out)];q=subprocess.run([a.vpp,*cmd],capture_output=True,text=True,env=env,timeout=30);assert q.returncode==0,q.stderr;(a.output/('unit'+str(i)+'.reference')).write_bytes(out.read_bytes());jobs.append('\x1f'.join(['-',*cmd]))
jf=a.output/'jobs';jf.write_text('\n'.join(jobs)+'\n');cache=[]
for name,extra in [('snapshot',{}),('replay',{'VCC_DR':'1','VCC_NODC':'1','VCC_DRCHECK':'1','VCC_DR_GAIN':'0'})]:
 clean={k:v for k,v in env.items() if k not in ['VCC_DR','VCC_NODC','VCC_DRCHECK','VCC_DR_GAIN']};cmd=[a.vpp,'--batch',str(jf),'-j','1','-C',str(a.output)];q=subprocess.run(cmd,env={**clean,**extra},capture_output=True,text=True,timeout=120);log=q.stdout+q.stderr;(a.output/(name+'.log')).write_text(log);assert q.returncode==0,log
 pattern=r'decl cache: [1-9][0-9]* restores' if name=='snapshot' else r'drcheck: [1-9][0-9]* regions compared, 0 mismatches';assert re.search(pattern,log),log
 for i in range(4):assert (a.output/('unit'+str(i)+'.s')).read_bytes()==(a.output/('unit'+str(i)+'.reference')).read_bytes(),(name,i)
 cache.append(dict(mode=name,cmd=cmd,rc=q.returncode,matched_units=4))
(a.output/'cache.json').write_text(json.dumps(cache,indent=2)+'\n');print('diagnostics',len(records),'errors',len(errors),'cache4TUs x2modes passed');raise SystemExit(bool(errors))
