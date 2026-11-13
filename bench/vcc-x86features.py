#!/usr/bin/env python3
"""Compare generated x86 CPU/feature macro data against Clang (six workers).
Without --vpp, compile a small probe of the generated tables and helper.
With --vpp, test the actual driver's -march and ordered -m options.
"""
import argparse, concurrent.futures, json, pathlib, platform, re, subprocess, tempfile
ROOT=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--clang',default='clang');p.add_argument('--vpp');p.add_argument('--all-features',action='store_true');a=p.parse_args()
empty=tempfile.NamedTemporaryFile(suffix='.c');empty.write(b'\n');empty.flush()
data=json.loads((ROOT/'bench/data/x86features-oracle.json').read_text())
controlled=set(data['feature_macros'])|set(data['cpu_macro_names'])
# These strings identify the compiler build that supplied the frozen tables,
# including its distribution packaging. They are not target-feature facts.
# Keep numeric compiler version macros and every target macro in the oracle.
BUILD_IDENTITY={'__VERSION__','__clang_version__'}
def macros(text):return {k:v for k,v in re.findall(r'^#define (\S+) ?(.*)$',text,re.M) if k not in BUILD_IDENTITY}
def run(args):
 r=subprocess.run(args,text=True,capture_output=True)
 if r.returncode:raise RuntimeError(' '.join(args)+'\n'+r.stderr)
 return r.stdout
baseline=macros(run([a.clang,'--target=x86_64-unknown-linux-gnu','-march=x86-64','-dM','-E','-x','c',empty.name]))
cases=[(cpu,[]) for cpu in data['cpus']]
if platform.machine().lower() in ('x86_64','amd64'):cases.append(('native',[]))
for cpu in ['x86-64','haswell','znver4','skylake-avx512']+(['native'] if platform.machine().lower() in ('x86_64','amd64') else []):
 for opts in (['-sse'],['-sse2'],['-mmx'],['+sse4.2','-popcnt'],['-popcnt','+sse4.2'],['+avx','-xsave'],['-xsave','+avx'],['-sse','+sse2'],['+sse2','-sse'],['+avx2','-avx'],['-avx','+avx2'],['-crc32','+sse4.2'],['+sse4.2','-sse4.1'],['+general-regs-only','+sse2'],['+sse2','+general-regs-only'],['+apxf','-egpr'],['-egpr','+apxf']):cases.append((cpu,opts))
if a.vpp:
 for feature in data['cli_features']:
  cases.extend([('x86-64',['+'+feature,'-'+feature]),('haswell',['-'+feature,'+'+feature])])
if a.vpp:cases=[(cpu,opts) for cpu,opts in cases if all(o[1:] in data['cli_features'] or o[1:]=='general-regs-only' for o in opts)]
if a.all_features:
 names=re.search(r'x86_feature_names\[\] = \{(.*?)\};',(ROOT/'src/include/arch/x64/x86features-data.inc').read_text()).group(1)
 for name in json.loads('['+names+']'):
  if name!='64bit' and (not a.vpp or name in data['cli_features']):cases.extend([('x86-64',['+'+name]),('haswell',['-'+name])])
with tempfile.TemporaryDirectory() as td:
 probe=pathlib.Path(td)/'probe'
 fixture=pathlib.Path(td)/'native-fixture'
 run([a.clang,'-O2','-Wall','-Wextra','-Werror','-I'+str(ROOT),'-I'+str(ROOT/'src/include'),str(ROOT/'bench/data/x86native-test.c'),'-o',str(fixture)])
 run([str(fixture)])
 if not a.vpp:
  src=pathlib.Path(td)/'probe.c'
  src.write_text(r'''
#include "common/buffer.h"
typedef vbuf buf;
#define bstr vbuf_s
#include "src/driver/x86predefines.h"
#include "port/x86native.h"
static void apply(x86_features *f,x86_features *disabled,const char *s) {
 bool en=*s++=='+';
 if (!strcmp(s,"general-regs-only")) { if(en){apply(f,disabled,"-x87");apply(f,disabled,"-mmx");apply(f,disabled,"-sse");} return; }
 if (!strcmp(s,"apxf")) { const char *subs[]={"egpr","push2pop2","ppx","ndd","ccmp","nf","zu","jmpabs"}; for(unsigned i=0;i<8;i++){char b[32];snprintf(b,sizeof b,"%c%s",en?'+':'-',subs[i]);apply(f,disabled,b);}return; }
 if (!strcmp(s,"sse4"))s=en?"sse4.2":"sse4.1";
 int bit=x86_feature_find(s);if(bit<0)abort();
 x86_feature_apply(f,bit,en);if(!en)disabled->bits[bit/64]|=UINT64_C(1)<<(bit%64);
}
int main(int argc,char **argv) {
 x86_features f={{0}},disabled={{0}};if(argc<2)return 2; const char *cpu=argv[1];
 if(!strcmp(cpu,"native")){if(!x86_native_features(&f,&disabled,&cpu))return 2;}else if(!x86_cpu_features(cpu,&f))return 2;
 for(int i=2;i<argc;i++)apply(&f,&disabled,argv[i]);
 x86_features_finish(&f,disabled);buf out={0};x86_feature_predefines(&out,f,cpu);fwrite(out.p,1,out.n,stdout);free(out.p);return 0;
}
''')
  run([a.clang,'-O2','-I'+str(ROOT),'-I'+str(ROOT/'src/include'),str(src),str(ROOT/'src/common/fail.c'),'-o',str(probe)])
 def check(case):
  cpu,opts=case
  ref=[a.clang,'--target=x86_64-unknown-linux-gnu','-march='+cpu,'-dM','-E','-x','c',empty.name]
  for o in opts:
   if a.vpp:ref+=[('-m'+o[1:] if o[0]=='+' else '-mno-'+o[1:])]
   else:ref+=['-Xclang','-target-feature','-Xclang',o]
  expected=macros(run(ref))
  if a.vpp:
   flags=[('-m'+o[1:] if o[0]=='+' else '-mno-'+o[1:]) for o in opts]
   actual=macros(run([a.vpp,'--target=x86_64-unknown-linux-gnu','-march='+cpu,*flags,'-dM','-E','-x','c',empty.name]))
  else:actual={**{k:v for k,v in baseline.items() if k not in controlled},**macros(run([str(probe),cpu,*opts]))}
  if actual!=expected:
   return f'{cpu} {" ".join(opts)}: missing={sorted(expected.keys()-actual.keys())}, extra={sorted(actual.keys()-expected.keys())}, values={[(k,actual[k],expected[k]) for k in actual.keys()&expected.keys() if actual[k]!=expected[k]]}'
 with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:failures=[x for x in pool.map(check,cases) if x]
 print(f'{len(cases)-len(failures)}/{len(cases)} macro cases pass')
 for f in failures:print(f)
 raise SystemExit(bool(failures))
