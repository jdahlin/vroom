#!/usr/bin/env python3
"""Wide static values against Python integers and IEEE packing; Clang x86 oracle.
ARM Clang cannot compile >128 BitInt: it compiles only the byte-checking caller.
"""
import argparse, pathlib, random, struct, subprocess, json, os, shutil
from hostenv import BUILD, llvm
p=argparse.ArgumentParser();p.add_argument('--bin-dir',default=BUILD);p.add_argument('--output',required=True);p.add_argument('--target',default='x86_64-linux-gnu',choices=['x86_64-linux-gnu','aarch64-linux-gnu']);a=p.parse_args()
out=pathlib.Path(a.output);out.mkdir(parents=True,exist_ok=True)
source=[];checks=[];count=0

def add(ty,expr,expected,bits):
 global count
 name='value'+str(count);count+=1;source.append(f'{ty} {name} = {expr};')
 n=(bits+7)//8; raw=int(expected&((1<<bits)-1)).to_bytes(n,'little')
 checks.append(f'extern unsigned char {name}[];')
 checks.append('static int check%d(void){static const unsigned char want[]={%s};for(unsigned i=0;i<%d;i++){unsigned mask=i==%d?%d:255;if((%s[i]&mask)!=(want[i]&mask))return %d;}return 0;}'%(count,','.join(map(str,raw)),n,n-1,(1<<(bits%8))-1 if bits%8 else 255,name,count))

rng=random.Random(4129)
for bits in [129,130,192,256,310,1020,4096]:
 ty=f'unsigned _BitInt({bits})';mask=(1<<bits)-1
 x=rng.getrandbits(bits);y=rng.getrandbits(bits)|1
 l=f'(({ty})0x{x:x}uwb)';r=f'(({ty})0x{y:x}uwb)'
 for op,value in [('+',x+y),('-',x-y),('*',x*y),('/',x//y),('%',x%y),('&',x&y),('|',x|y),('^',x^y)]:add(ty,f'{l}{op}{r}',value,bits)
 add(ty,f'{l}<<65',x<<65,bits);add(ty,f'{l}>>67',x>>67,bits)
 add(f'_BitInt({bits})','-100',-100,bits)
for ty,expr,value,bits in [('_BitInt(310)','0x1.8p200',3<<199,310),('_BitInt(310)','-0x1.8p200',-(3<<199),310),('unsigned _BitInt(256)','123.875',123,256)]:add(ty,expr,value,bits)
def ieee_integer(x, fraction, bias):
 if not x:return 0
 exponent=x.bit_length()-1;shift=max(exponent-fraction,0);sig=x>>shift
 if shift:
  rem=x&((1<<shift)-1);half=1<<(shift-1)
  if rem>half or (rem==half and sig&1):sig+=1
 if sig>>(fraction+1):sig>>=1;exponent+=1
 if exponent+bias>=2*bias+1:return (2*bias+1)<<fraction
 return ((exponent+bias)<<fraction)|(sig&((1<<fraction)-1))
for bits in [129,256,310,1020]:
 for x in [1<<100,(1<<100)+(1<<47)+1,(1<<100)+(1<<47),(1<<100)+(1<<76)+1,(1<<100)+(1<<76),(1<<(bits-2))+123]:
  for ty,fmt in [('float','f'),('double','d')]:
   value=ieee_integer(x,23 if fmt=='f' else 52,127 if fmt=='f' else 1023)
   add(ty,f'(unsigned _BitInt({bits}))0x{x:x}uwb',value,32 if fmt=='f' else 64)
sourcepath=out/'values.c';sourcepath.write_text('\n'.join(source)+'\n')
caller=out/'caller.c';caller.write_text('\n'.join(checks)+'\nint main(void){'+''.join(f'if(check{i}())return {i};' for i in range(1,count+1))+'return 0;}\n')
records=[]
def run(cmd):
 q=subprocess.run(list(map(str,cmd)),capture_output=True,text=True,timeout=60);records.append({'argv':list(map(str,cmd)),'rc':q.returncode,'stderr':q.stderr,'stdout':q.stdout});
 if q.returncode:raise RuntimeError(str(cmd)+'\n'+q.stderr+'\n'+q.stdout)
try:
 target='--target='+a.target;cl=llvm('clang');vpp=str(pathlib.Path(a.bin_dir)/'vpp')
 compilers=[('vcc',vpp)]+([('clang',cl)] if a.target.startswith('x86') else [])
 for name,compiler in compilers:
  asm=out/(name+'.s');obj=out/(name+'.o');exe=out/(name+'.exe')
  run([compiler,target,'-std=c23','-w','-S',sourcepath,'-o',asm]);run([cl,target,'-c',asm,'-o',obj]);run([cl,target,'-fuse-ld=lld','-no-pie',caller,obj,'-o',exe]);run((['qemu-aarch64','-L','/usr/aarch64-linux-gnu'] if a.target.startswith('aarch64') else [])+[exe])
 testdir=pathlib.Path(__file__).resolve().parent.parent/'ccs/vcc/tests/bitint'
 for name,compiler in compilers:
  run([compiler,target,'-std=c23','-fsyntax-only',testdir/'constant-valid.c'])
  cmd=[compiler,target,'-std=c23','-fsyntax-only',str(testdir/'constant-reject.c')]
  q=subprocess.run(cmd,capture_output=True,text=True,timeout=60)
  records.append(dict(argv=cmd,rc=q.returncode,stderr=q.stderr,stdout=q.stdout))
  import re
  lines=[int(n) for n in re.findall(r'constant-reject\.c:(\d+):\d+: error:',q.stderr)]
  if q.returncode==0 or sorted(lines)!=[2,3,5,6,8]:raise RuntimeError('constexpr constraint mismatch: '+q.stderr)
 cmd=[vpp,target,'-std=c23','-fsyntax-only',str(testdir/'vector-reject.c')]
 q=subprocess.run(cmd,capture_output=True,text=True,timeout=60)
 records.append(dict(argv=cmd,rc=q.returncode,stderr=q.stderr,stdout=q.stdout))
 lines=[int(n) for n in re.findall(r'vector-reject\.c:(\d+):\d+: error: vector integer elements wider than 128 bits are not supported',q.stderr)]
 if q.returncode==0 or lines!=[2,3,4,5] or q.stderr.count(': error:')!=4:raise RuntimeError('wide vector rejection mismatch: '+q.stderr)
 for filename,expected,number in [
  ('runtime-to-float-reject.c','floating conversion',1),
  ('runtime-from-float-reject.c','floating conversion',1),
  ('switch-large-reject.c','wide integer constant is not representable in this 128-bit constant-expression context',4)]:
  cmd=[vpp,target,'-std=c23','-S',str(testdir/filename),'-o',str(out/'unsupported.s')]
  q=subprocess.run(cmd,capture_output=True,text=True,timeout=60)
  records.append(dict(argv=cmd,rc=q.returncode,stderr=q.stderr,stdout=q.stdout))
  if q.returncode<=0 or q.stderr.count(expected)!=number:raise RuntimeError('unsupported contract mismatch: '+q.stderr)
 print(f'{count} static values and constexpr contracts passed: '+', '.join(n for n,_ in compilers))
finally:(out/'commands.json').write_text(json.dumps(records,indent=2))
