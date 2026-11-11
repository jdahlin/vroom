#!/usr/bin/env python3
"""Wide immutable word-pool values survive snapshot and declaration replay."""
import argparse, pathlib, subprocess, os, json, re
from hostenv import BUILD
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--bin-dir', default=BUILD)
parser.add_argument('--output',required=True)
args=parser.parse_args()
out=pathlib.Path(args.output).resolve();out.mkdir(parents=True,exist_ok=True)
bin=pathlib.Path(args.bin_dir).resolve()/'vpp'
h=out/'common.h'
h.write_text('\n'.join(f'constexpr unsigned _BitInt(310) item{i}=((unsigned _BitInt(310))1<<200)+{i};\nstatic_assert((item{i}>>200)==1);' for i in range(180)))
h.write_text(h.read_text()+'''
static inline float wide_fp(unsigned _BitInt(310) x) { return x; }
static inline unsigned _BitInt(310) wide_int(double x) { return x; }
static inline double wide_constant(void) { return (unsigned _BitInt(310))1 << 200; }
''')
records=[]
for target in ['x86_64-linux-gnu','aarch64-linux-gnu']:
 jobs=[]
 for i in range(8):
  p=out/f'tu{i}.c';p.write_text(f'#include "common.h"\nunsigned _BitInt(310) result{i}=item179;\nfloat fp{i}(unsigned _BitInt(310) x) {{ return wide_fp(x); }}\nunsigned _BitInt(310) ip{i}(double x) {{ return wide_int(x); }}\ndouble cp{i}(void) {{ return wide_constant(); }}\n')
  args=[f'--target={target}','-std=c23','-S',str(p),'-o',str(out/f'{target}-{i}.s')]
  q=subprocess.run([str(bin),*args],capture_output=True,text=True);assert q.returncode==0,q.stderr
  (out/f'{target}-{i}.reference').write_bytes((out/f'{target}-{i}.s').read_bytes())
  jobs.append('\x1f'.join(['-']+args))
 jf=out/'jobs';jf.write_text('\n'.join(jobs)+'\n')
 for mode,env in [('snapshot',{}),('replay',{'VCC_DR':'1','VCC_NODC':'1','VCC_DRCHECK':'1','VCC_DR_GAIN':'0'})]:
  q=subprocess.run([str(bin),'--batch',str(jf),'-j','1','-C',str(out)],env={**os.environ,**env},capture_output=True,text=True)
  (out/f'{target}-{mode}.log').write_text(q.stdout+q.stderr)
  assert q.returncode==0,q.stderr
  if mode=='snapshot':assert re.search(r'decl cache: [1-9][0-9]* restores',q.stdout),q.stdout
  else:assert re.search(r'drcheck: [1-9][0-9]* regions compared, 0 mismatches',q.stdout),q.stdout
  for i in range(8):assert (out/f'{target}-{i}.reference').read_bytes()==(out/f'{target}-{i}.s').read_bytes(),(target,mode,i)
  records.append(dict(target=target,mode=mode,stdout=q.stdout))
(out/'results.json').write_text(json.dumps(records,indent=2))
print('8 translation units x 2 targets x snapshot/replay: assembly matches per-process')
