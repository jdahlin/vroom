#!/usr/bin/env python3
"""Compare retained x86 function target attributes and inline constraints with clang."""
import argparse, itertools, os, pathlib, platform, re, subprocess, tempfile
p=argparse.ArgumentParser();p.add_argument('--vpp',required=True);p.add_argument('--clang',default=os.environ.get('LLVM','/usr/lib/llvm-23')+'/bin/clang');a=p.parse_args()
cases=[([],x,y) for x,y in itertools.product(['sse2','sse4.1','sse4.2','avx2','no-sse2'], repeat=2)]
cases += [(f,x,y) for f in [['-msse4.2'],['-mno-sse2'],['-mno-popcnt'],['-mavx2']] for x,y in [('sse4.2','sse2'),('arch=nehalem','sse4.2'),('arch=core2','sse2'),('no-sse2,sse4.2','sse4.2'),('sse4.2,no-sse2','sse2')]]
fail=0
with tempfile.TemporaryDirectory(prefix='vcc-x86target-') as td:
 src=pathlib.Path(td)/'probe.c';out=pathlib.Path(td)/'probe.s'
 for flags,callee,caller in cases:
  src.write_text('__attribute__((target("'+callee+'"),always_inline)) inline int callee(int x){return x+1;}\n__attribute__((target("'+caller+'"))) int caller(int x){return callee(x);}\n')
  for mode in ['-S','-fsyntax-only']:
   results=[]
   for exe in [a.clang,a.vpp]:
    r=subprocess.run([exe,'--target=x86_64-linux-gnu','-O2',*flags,mode,str(src),'-o',str(out)],capture_output=True,text=True)
    results.append(r)
   if bool(results[0].returncode)!=bool(results[1].returncode):
    fail+=1;print('FAIL',flags,callee,caller,mode);print('clang:',results[0].stderr,'vcc:',results[1].stderr)
 # The immediate inline caller, not the outer physical function, owns features.
 nested_checks = cache_checks = 0
 for inner, middle, outer in [('avx2','no-avx','avx2'),('sse4.2','sse2','sse4.2'),('avx2','avx2','avx2'),('sse2','no-avx','avx2')]:
  header = ('__attribute__((always_inline,target("'+inner+'"))) static inline int f(int x){return x+1;}\n'
            '__attribute__((always_inline,target("'+middle+'"))) static inline int g(int x){return f(x);}\n')
  body = '__attribute__((target("'+outer+'"))) int h(int x){return g(x);}\n'
  src.write_text(header+body)
  rejected = False
  for mode in ['-S','-fsyntax-only']:
   results = [subprocess.run([exe,'--target=x86_64-linux-gnu','-O2',mode,str(src),'-o',str(out)],capture_output=True,text=True) for exe in [a.clang,a.vpp]]
   nested_checks += 1
   if mode=='-S': rejected=bool(results[0].returncode)
   if bool(results[0].returncode)!=bool(results[1].returncode):
    fail+=1;print('FAIL nested target',inner,middle,outer,mode,results[1].stderr)
  hp=pathlib.Path(td)/'nested.h'
  hp.write_text('#ifndef NESTED_H\n#define NESTED_H\n'+''.join('typedef struct { int a[%d]; } T%d;\n'%(i+1,i) for i in range(100))+header+'#endif\n')
  src.write_text('#include "nested.h"\n'+body)
  jobs=pathlib.Path(td)/'jobs'
  jobs.write_text(('\x1f'.join(['-','--target=x86_64-linux-gnu','-O2','-S',str(src)])+'\n')*3)
  for replay in [False,True]:
   env=os.environ.copy()
   for key in ['VCC_NODC','VCC_DR','VCC_DRREC','VCC_DRCHECK','VCC_DR_GAIN']:env.pop(key,None)
   env['VPP_BATCH_ERR']='1'
   if replay:env.update(VCC_NODC='1',VCC_DR='1',VCC_DR_GAIN='0',VCC_DRCHECK='1')
   r=subprocess.run([a.vpp,'--batch',str(jobs),'-j','1','--warm','1'],env=env,capture_output=True,text=True)
   hit=re.search(r'replayed ([1-9][0-9]*)' if replay else r'decl cache: ([1-9][0-9]*) restores',r.stdout)
   cache_checks += 1
   if bool(r.returncode)!=rejected or (('0/3 ok' if rejected else '3/3 ok') not in r.stdout) or not hit or (replay and '0 mismatches' not in r.stdout):
    fail+=1;print('FAIL nested target cache',inner,middle,outer,replay,r.stdout,r.stderr)
 print(f'nested inline targets: {nested_checks} oracle checks, {cache_checks} verified snapshot/replay paths')
 # Wide-vector AVX ABI is deliberately diagnosed until YMM/ZMM calls exist.
 guards=[('-mavx', 'typedef float V __attribute__((vector_size(32))); V f(V x){return x;}'),
         ('-mavx512f', 'typedef float V __attribute__((vector_size(64))); V f(V x){return x;}'),
         ('-msse2', 'typedef float V __attribute__((vector_size(32))); __attribute__((target("avx"))) extern V f(V); V g(V x){return f(x);}')]
 for flag,code in guards:
  src.write_text(code)
  r=subprocess.run([a.vpp,'--target=x86_64-linux-gnu',flag,'-S',str(src),'-o',str(out)],capture_output=True,text=True)
  if not r.returncode or 'AVX vector argument or return ABI' not in r.stderr:
   fail+=1;print('FAIL AVX ABI guard',flag,r.stderr)
 src.write_text('__attribute__((target("sse2"))) double f(double a,double b){return a+b;}')
 for exe in [a.clang,a.vpp]:
  r=subprocess.run([exe,'--target=x86_64-linux-gnu','-mno-sse2','-S',str(src),'-o',str(out)],capture_output=True,text=True)
  if r.returncode:fail+=1;print('FAIL target SIMD override',exe,r.stderr)
 # Clang's LLVM signature is the ABI oracle for aggregate register transport.
 records = [
  ('struct', 'V32 v;'), ('union', 'V32 v; V32 w;'),
  ('struct', 'V32 v[1];'), ('struct', 'struct { V32 v; } n;'),
  ('union', 'V32 v; float f;'), ('union', 'V32 v; __float128 f;'),
  ('union', 'V32 v; int i;'), ('union', 'V32 v; long double f;'),
  ('struct', 'V16 v[2];'), ('struct', 'long x[4];'),
  ('struct', 'V64 v;'), ('struct', 'V64 v[1];'),
  ('struct', 'V32 v[2];'), ('union', 'V64 v; V32 w;'),
  ('union', 'V64 v; struct { V32 a,b; } w;'),
  ('struct __attribute__((packed))', 'V32 v;'),
  ('struct __attribute__((aligned(32)))', 'V16 v;'),
  ('union', 'V32 v; struct { unsigned x:1; } b;'),
  ('union', 'V32 v; V16 a[2];'),
  ('union', 'V32 v; long a[4];'),
  ('union', 'V32 v; struct { V16 a[2]; } s;'),
  ('union', 'V32 v; unsigned :1;'),
  ('struct', 'V32 v; unsigned :0;'),
 ]
 aggregate_checks = 0
 for kind, fields in records:
  pre = ('typedef float V16 __attribute__((vector_size(16))); '
         'typedef float V32 __attribute__((vector_size(32))); '
         'typedef float V64 __attribute__((vector_size(64))); '
         + 'typedef '+kind+' { '+fields+' } R; ')
  for feature in ['sse2','avx','avx512f']:
   src.write_text(pre+'R probe(R x){return x;}')
   oracle = subprocess.run([a.clang,'--target=x86_64-linux-gnu','-m'+feature,'-S','-emit-llvm',str(src),'-o','-'],capture_output=True,text=True)
   signature = next((line for line in oracle.stdout.splitlines() if line.startswith('define ')), '')
   registers = bool(signature) and 'sret(' not in signature
   if oracle.returncode or not signature:
    fail+=1;print('FAIL aggregate oracle',kind,fields,feature,oracle.stderr);continue
   for form in ['global','attribute','external']:
    flags = ['-m'+feature] if form=='global' else []
    attr = '__attribute__((target("'+feature+'"))) '
    body = ('R probe(R x){return x;}' if form=='global' else
            attr+'R probe(R x){return x;}' if form=='attribute' else
            attr+'extern R callee(R); R probe(R x){return callee(x);}')
    src.write_text(pre+body)
    r=subprocess.run([a.vpp,'--target=x86_64-linux-gnu',*flags,'-S',str(src),'-o',str(out)],capture_output=True,text=True)
    rejected = bool(r.returncode) and 'AVX vector argument or return ABI' in r.stderr
    aggregate_checks += 1
    if rejected != registers or (r.returncode and not rejected):
     fail+=1;print('FAIL aggregate ABI',kind,fields,feature,form,'registers=',registers,r.stderr)
 # Natural vector alignment is independent of whether AVX instructions are enabled.
 alignment_checks = 0
 for width in [32,64]:
  src.write_text('typedef float V __attribute__((vector_size('+str(width)+'))); struct R {char c; V v;}; '
                 '_Static_assert(_Alignof(V)=='+str(width)+',"vector alignment"); '
                 '_Static_assert(__builtin_offsetof(struct R,v)=='+str(width)+',"field offset"); '
                 '_Static_assert(sizeof(struct R)=='+str(width*2)+',"record size");')
  for flags in [[],['-mavx'],['-mavx512f']]:
   for tool in [a.clang,a.vpp]:
    r=subprocess.run([tool,'--target=x86_64-linux-gnu',*flags,'-fsyntax-only',str(src)],capture_output=True,text=True)
    alignment_checks += 1
    if r.returncode:fail+=1;print('FAIL wide-vector layout',width,flags,tool,r.stderr)
 print(f'wide-vector layout: {alignment_checks} oracle checks')
 # Unnamed wide-vector arguments retain stack transport even with AVX enabled.
 variadic_checks = variadic_runs = 0
 for width, feature in [(32,'avx'),(64,'avx512f')]:
  for shape in ['vector','struct','union']:
   pre='typedef float V __attribute__((vector_size('+str(width)+'))); '
   pre+= 'typedef V A; ' if shape=='vector' else 'typedef '+shape+' { V v; } A; '
   field='' if shape=='vector' else '.v'
   callee=pathlib.Path(td)/'callee.c'; obj=pathlib.Path(td)/'callee.o'; exe=pathlib.Path(td)/'variadic'
   callee.write_text(pre+'int consume(int n,...){__builtin_va_list ap;__builtin_va_start(ap,n);A x=__builtin_va_arg(ap,A);__builtin_va_end(ap);for(int i=0;i<'+str(width//4)+';i++)if(x'+field+'[i]!=i+1)return 1;return 0;}')
   r=subprocess.run([a.clang,'--target=x86_64-linux-gnu','-Wno-psabi','-O0','-c',str(callee),'-o',str(obj)],capture_output=True,text=True)
   if r.returncode:fail+=1;print('FAIL variadic oracle callee',r.stderr);continue
   for indirect in [False,True]:
    call='consume(1,x)' if not indirect else 'p(1,x)'
    body='int main(void){A x;for(int i=0;i<'+str(width//4)+';i++)x'+field+'[i]=i+1;int(*p)(int,...)=consume;return '+call+';}'
    src.write_text(pre+'extern int consume(int,...);'+body)
    for tool in [a.clang,a.vpp]:
     r=subprocess.run([tool,'--target=x86_64-linux-gnu','-m'+feature,'-O0','-S',str(src),'-o',str(out)],capture_output=True,text=True)
     variadic_checks += 1
     if r.returncode:fail+=1;print('FAIL unnamed variadic',shape,width,indirect,tool,r.stderr);continue
     if tool==a.vpp and platform.system()=='Linux' and platform.machine().lower() in ['x86_64','amd64']:
      # The callee uses baseline instructions; no host AVX/AVX512 is required.
      link=subprocess.run([a.clang,'-no-pie',str(out),str(obj),'-o',str(exe)],capture_output=True,text=True)
      if link.returncode:fail+=1;print('FAIL variadic link',link.stderr);continue
      result=subprocess.run([str(exe)],capture_output=True,text=True)
      variadic_runs += 1
      if result.returncode:fail+=1;print('FAIL variadic transport',shape,width,indirect,result.returncode)
   # Reverse the cross-compiler direction to check va_arg's overflow cursor.
   vc=pathlib.Path(td)/'callee-vcc.s'; caller=pathlib.Path(td)/'caller-clang.o'
   r=subprocess.run([a.vpp,'--target=x86_64-linux-gnu','-m'+feature,'-O0','-S',str(callee),'-o',str(vc)],capture_output=True,text=True)
   variadic_checks += 1
   if r.returncode:fail+=1;print('FAIL variadic receiver',shape,width,r.stderr)
   elif platform.system()=='Linux' and platform.machine().lower() in ['x86_64','amd64']:
    r=subprocess.run([a.clang,'--target=x86_64-linux-gnu','-Wno-psabi','-O0','-c',str(src),'-o',str(caller)],capture_output=True,text=True)
    if not r.returncode:r=subprocess.run([a.clang,'-no-pie',str(caller),str(vc),'-o',str(exe)],capture_output=True,text=True)
    if r.returncode:fail+=1;print('FAIL reverse variadic link',r.stderr)
    else:
     r=subprocess.run([str(exe)],capture_output=True,text=True);variadic_runs+=1
     if r.returncode:fail+=1;print('FAIL reverse variadic transport',shape,width,r.returncode)
   for result in [False,True]:
    code='extern A named(int,...); A f(void){return named(1,2);}' if result else 'extern int named(A,...); int f(A x){return named(x,1);}'
    src.write_text(pre+code)
    r=subprocess.run([a.vpp,'--target=x86_64-linux-gnu','-m'+feature,'-S',str(src),'-o',str(out)],capture_output=True,text=True)
    variadic_checks += 1
    if not r.returncode or 'AVX vector argument or return ABI' not in r.stderr:
     fail+=1;print('FAIL named variadic ABI guard',shape,width,result,r.stderr)
 print(f'variadic AVX ABI: {variadic_checks} compile/guard checks, {variadic_runs} native cross-compiler executions')
print(f'aggregate AVX ABI: {aggregate_checks} oracle/diagnostic comparisons; total failures {fail}')
print(f'x86 target attributes: {2*len(cases)} oracle comparisons, 3 ABI guards, 2 SIMD overrides; {fail} failures')
raise SystemExit(bool(fail))
