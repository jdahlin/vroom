#!/usr/bin/env python3
"""Independent byte-exact runtime oracle for every lowered X9 descriptor.
Uses frozen generated descriptor input, not the lowering instruction/order map.
Clang compiles the same builtin expressions; a Clang-built driver supplies raw
IEEE/integer boundary inputs and records result bytes plus argument evaluations.
"""
import argparse,csv,hashlib,json,os,pathlib,re,subprocess
p=argparse.ArgumentParser();p.add_argument('--descriptors',type=pathlib.Path,required=True);p.add_argument('--vpp',type=pathlib.Path,required=True);p.add_argument('--output',type=pathlib.Path,required=True);p.add_argument('--clang',default='/usr/lib/llvm-23/bin/clang');p.add_argument('--opt',default='-O2');a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
env={**os.environ,'TMPDIR':os.environ.get('TMPDIR', '/tmp')};steps=[]
def run(cmd):
 q=subprocess.run([str(x) for x in cmd],env=env,capture_output=True,timeout=120);steps.append({'argv':list(map(str,cmd)),'exit':q.returncode,'stdout':q.stdout.decode(errors='replace'),'stderr':q.stderr.decode(errors='replace')});(a.output/'commands.json').write_text(json.dumps(steps,indent=2)+'\n')
 if q.returncode:raise RuntimeError({'argv':steps[-1]['argv'],'exit':q.returncode,'stderr':steps[-1]['stderr'][-4000:]})
 return q.stdout
rows=[]
for line in a.descriptors.read_text().splitlines():
 if line.startswith('X86_INTRINSIC('):
  x=next(csv.reader([line[len('X86_INTRINSIC('):-1]],skipinitialspace=True))
  if x[4]!='XI_UNSUPPORTED':rows.append(dict(id=x[0],name=x[1],sig=x[2].split(),features=x[3],kind=x[4],constraints=x[8]))
assert rows
# Runtime CPU gate is executed by a Clang-built helper; no guessed host model.
features=sorted({f for r in rows for f in r['features'].split(',') if f})
checks=[]
for f in features:
 assert '|' not in f,f
 checks.append('if(!__builtin_cpu_supports("'+('sse4.2' if f=='crc32' else f)+'"))return 77;')
(a.output/'cpu.c').write_text('int main(void){__builtin_cpu_init();'+''.join(checks)+'return 0;}')
run([a.clang,a.output/'cpu.c','-o',a.output/'cpu']);run([a.output/'cpu'])
scalars={'c':'char','a':'signed char','C':'unsigned char','s':'short','S':'unsigned short','i':'int','I':'unsigned int','l':'long','L':'unsigned long','q':'long long','Q':'unsigned long long','f':'float','d':'double'};sizes=dict(c=1,a=1,C=1,s=2,S=2,i=4,I=4,l=8,L=8,q=8,Q=8,f=4,d=8)
types={t.lstrip('!') for r in rows for t in r['sig']};defs=[]
for t in sorted(types):
 m=re.fullmatch(r'V(\d+)([a-zA-Z])',t)
 if m:defs.append('typedef '+scalars[m[2]]+' '+t+' __attribute__((vector_size('+str(int(m[1])*sizes[m[2]])+')));')
 else:defs.append('typedef '+scalars[t]+' '+t+';')
code=['\n'.join(defs),'static volatile unsigned counts[3];']
for t in sorted(types):
 code.append('__attribute__((noinline)) static '+t+' get_'+t+'(const unsigned char*p,unsigned k){'+t+' x;__builtin_memcpy(&x,p,sizeof x);counts[k]++;return x;}')
for idx,r in enumerate(rows):
 sig=r['sig'];ret=sig[0];args=sig[1:];consts=[i for i,t in enumerate(args) if t.startswith('!')];assert len(consts)<=1
 vals=[0]
 if consts:
  lo,hi=0,255
  if r['constraints']:
   c=r['constraints'].split(';')[0].split(':');lo,hi=int(c[1]),int(c[2])
  vals=sorted({v for v in [lo,hi,1,3,7,15,16,31,63,127,128,255] if lo<=v<=hi})
 r['immediates']=vals
 start='void test_'+str(idx)+'(unsigned char*out,const unsigned char*a,const unsigned char*b,unsigned sel){counts[0]=counts[1]=counts[2]=0;'+ret+' result;'
 for phase in range(2):
  code.append(start.replace('test_'+str(idx),'test_'+str(idx)+'_'+str(phase)))
  # Copies made before nested argument calls must remain live after the builtin.
  for j,t in enumerate(args):
   if not t.startswith('!'):code.append(t+' live'+str(j)+';__builtin_memcpy(&live'+str(j)+','+('a' if j==0 else 'b')+',sizeof live'+str(j)+');')
  code.append('switch(sel%'+str(len(vals))+'){')
  for k,v in enumerate(vals):
   aa=[]
   for j,t in enumerate(args):
    if t.startswith('!'):aa.append(str(v))
    elif phase:aa.append('get_'+t+'('+('a' if j==0 else 'b')+','+str(j)+')')
    else:aa.append('live'+str(j))
   code.append('case '+str(k)+':result='+r['name']+'('+','.join(aa)+');break;')
  code.append('}__builtin_memset(out,0,64);__builtin_memcpy(out,&result,sizeof result);')
  for j,t in enumerate(args):
   if not t.startswith('!'):code.append('__builtin_memcpy(out+'+str(16+j*16)+',&live'+str(j)+',sizeof live'+str(j)+');')
  code.append('for(unsigned k=0;k<3;k++)out[60+k]=(unsigned char)counts[k];}')
(a.output/'wrappers.c').write_text('\n'.join(code)+'\n')
driver=['extern int printf(const char*,...);typedef void(*F)(unsigned char*,const unsigned char*,const unsigned char*,unsigned);']
for i in range(len(rows)):
 for phase in range(2):driver.append('extern void test_'+str(i)+'_'+str(phase)+'(unsigned char*,const unsigned char*,const unsigned char*,unsigned);')
driver.append('F funcs[]={'+','.join('test_'+str(i)+'_'+str(phase) for i in range(len(rows)) for phase in range(2))+'};')
driver.append('unsigned immeds[]={'+','.join(str(len(r['immediates'])) for r in rows)+'};')
driver.append('''
static unsigned long long patterns[]={0,1,15,16,31,32,63,64,65,127,128,255,0xffffffffffffffffULL,0x8000000080000000ULL,0x7fffffff7fffffffULL,0x3f8000003f800000ULL,0xbf800000bf800000ULL,0x7fc123457fc12345ULL,0x7fc543217fc54321ULL,0xffc12345ffc12345ULL,0x7f800000ff800000ULL,0x0000000100000001ULL,0x3ff0000000000000ULL,0xbff0000000000000ULL,0x7ff8123456789abcULL,0xfff8543212345678ULL,0x7ff0000000000000ULL,0xfff0000000000000ULL,0x8000000000000000ULL};
int main(void){unsigned char a[16],b[16],out[64];unsigned long long seed=0x123456789abcdefULL;
for(unsigned f=0;f<sizeof funcs/sizeof*funcs;f++)for(unsigned sample=0;sample<192;sample++){
 for(unsigned j=0;j<2;j++){seed^=seed<<13;seed^=seed>>7;seed^=seed<<17;
 unsigned long long x=sample<29?patterns[sample]:seed;
 unsigned long long y=sample<29?patterns[(sample+1+j)%29]:~seed;
 if(sample>=29&&sample<58){x=patterns[sample-29];y=patterns[sample-29];}
 if(sample>=58&&sample<87){x=patterns[sample-58];y=patterns[(sample-58+14)%29];}
 if(sample==87){x=0;y=0x8000000080000000ULL;}
 if(sample==88){x=0x8000000080000000ULL;y=0;}
 if(sample==89){x=0;y=0x8000000000000000ULL;}
 if(sample==90){x=0x8000000000000000ULL;y=0;}
 if(sample==91){x=0x7fc123457fc12345ULL;y=0x7fc543217fc54321ULL;}
 if(sample==92){x=0x7fc543217fc54321ULL;y=0x7fc123457fc12345ULL;}
 if(sample==93){x=0x7ff8123456789abcULL;y=0x7ff8543212345678ULL;}
 if(sample==94){x=0x7ff8543212345678ULL;y=0x7ff8123456789abcULL;}
 __builtin_memcpy(a+8*j,&x,8);__builtin_memcpy(b+8*j,&y,8);}
 for(unsigned im=0;im<immeds[f/2];im++){funcs[f](out,a,b,im);printf("%u %u %u ",f,sample,im);for(unsigned k=0;k<64;k++)printf("%02x",out[k]);printf("\\n");}}
return 0;}
''')
(a.output/'driver.c').write_text('\n'.join(driver))
flags=['--target=x86_64-linux-gnu',a.opt,'-msse4.2','-mcrc32']
run([a.clang,'-O2','-c',a.output/'driver.c','-o',a.output/'driver.o'])
outputs={};errors=[]
for mode,cc in [('clang',a.clang),('object',a.vpp),('assembly',a.vpp)]:
 try:
  obj=a.output/(mode+'.o')
  if mode=='assembly':run([cc,*flags,'-S',a.output/'wrappers.c','-o',a.output/'wrappers.s']);run([a.clang,'-c',a.output/'wrappers.s','-o',obj])
  else:run([cc,*flags,'-c',a.output/'wrappers.c','-o',obj])
  exe=a.output/mode;run([a.clang,obj,a.output/'driver.o','-o',exe]);outputs[mode]=run([exe]);(a.output/(mode+'.stdout')).write_bytes(outputs[mode])
 except RuntimeError as error:
  errors.append({"mode":mode,"error":str(error)})

fail=[]
for mode in ['object','assembly']:
 if mode not in outputs:continue
 for ref,got in zip(outputs['clang'].splitlines(),outputs[mode].splitlines()):
  if ref!=got:
   index=int(ref.split()[0]);fail.append({'mode':mode,'builtin':rows[index//2]['name'],'phase':index%2,'expected':ref.decode(),'actual':got.decode()})
 assert len(outputs[mode].splitlines())==len(outputs['clang'].splitlines())
summary={'mapped':len(rows),'rows':rows,'features':features,'opt':a.opt,'binary_sha256':hashlib.sha256(a.vpp.read_bytes()).hexdigest(),'descriptor_sha256':hashlib.sha256(a.descriptors.read_bytes()).hexdigest(),'reference_checks':len(outputs['clang'].splitlines()),'compared_modes':[m for m in outputs if m!='clang'],'checks_per_completed_mode':len(outputs['clang'].splitlines()),'mismatches':fail,'errors':errors}
(a.output/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print('mapped',len(rows),'completed modes',summary['compared_modes'],'checks/completed mode',summary['checks_per_completed_mode'],'mismatches',len(fail),'failed modes',[x['mode'] for x in errors]);print(json.dumps(fail[:5],indent=2));raise SystemExit(bool(fail or errors))
