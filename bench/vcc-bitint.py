#!/usr/bin/env python3
"""Wide integer arithmetic against independent Python integer expectations.

Clang compiles the ordinary-C caller on both targets. Only x86 has a native
Clang wide-BitInt implementation, checked as a second implementation there.
ARM's arithmetic oracle is Python, not an unsupported Clang language feature.
"""
import argparse, concurrent.futures, importlib.util, json, os, pathlib, random


def expected(a,b,bits,shift):
    mask=(1<<bits)-1
    sa=a-(1<<bits) if a>>(bits-1) else a
    sb=b-(1<<bits) if b>>(bits-1) else b
    q=abs(sa)//abs(sb)
    if (sa<0)!=(sb<0): q=-q
    values=[a+b,a-b,a*b,a&b,a|b,a^b,a<<shift,a>>shift,a//b,a%b,
            -a,~a,sa>>shift,q,sa-q*sb]
    cmp=(a==b)|((a<b)<<1)|((a>b)<<2)|((sa<sb)<<3)|((sa>sb)<<4)|((not a)<<5)|((not sa)<<6)
    return [v&mask for v in values],cmp


def caller(bits):
    rng=random.Random(bits); mask=(1<<bits)-1; n=(bits+63)//64
    values=[0,1,2,(1<<63)-1,1<<64,(1<<(bits-1))-1,1<<(bits-1),mask]
    pairs=[(a,b) for a in values for b in values if b and not(a==(1<<(bits-1)) and b==mask)]
    pairs += [(rng.getrandbits(bits),rng.getrandbits(bits)|1) for _ in range(20)]
    shifts=[0,1,31,32,63,64,65,bits-1]
    def words(v): return '{'+','.join('0x%xULL'%((v>>(64*i))&((1<<64)-1)) for i in range(n))+'}'
    rows=[]
    for i,(a,b) in enumerate(pairs):
        shift=shifts[i%len(shifts)]
        exp,cmp=expected(a,b,bits,shift)
        # ABI-visible padding need not be canonical. Mix zero and one padding.
        dirty=((1<<(n*64))-1)^mask
        rows.append('{%s,%s,{%s},%d,%d}'%(words(a|(dirty if i&1 else 0)),words(b|(dirty if i&2 else 0)),','.join(words(v) for v in exp),shift,cmp))
    return '''#include <stdio.h>
extern void wide_ops(const unsigned long long*,const unsigned long long*,unsigned,unsigned long long*,unsigned*);
extern unsigned wide_control(void);
extern unsigned wide_counts(void);
#define N %d
static const struct { unsigned long long a[N],b[N],want[15][N]; unsigned shift,cmp; } cases[] = {
%s
};
int main(void) {
 unsigned counts=wide_counts(); if(counts) {printf("counts %%u\\n",counts);return 4;}
 unsigned control=wide_control(); if(control) {printf("control %%u\\n",control);return 3;}
 for(unsigned i=0;i<sizeof cases/sizeof cases[0];i++) {
  unsigned long long out[15][N]; unsigned cmp;
  wide_ops(cases[i].a,cases[i].b,cases[i].shift,&out[0][0],&cmp);
  for(unsigned op=0;op<15;op++) for(unsigned j=0;j<N;j++) {
   unsigned long long mask = j==N-1 ? 0x%xULL : ~0ULL;
   if((out[op][j]&mask)!=cases[i].want[op][j]) {
    printf("case %%u op %%u limb %%u: %%llx != %%llx\\n",i,op,j,out[op][j]&mask,cases[i].want[op][j]); return 1;
   }
  }
  if(cmp!=cases[i].cmp) {printf("case %%u comparison %%u != %%u\\n",i,cmp,cases[i].cmp);return 2;}
 }
 return 0;
}
'''%(n,',\n'.join(rows),mask>>(64*(n-1)))


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--target',choices=['x86_64','aarch64'],default='x86_64')
    ap.add_argument('--bin-dir',required=True)
    ap.add_argument('--output',type=pathlib.Path,required=True)
    ap.add_argument('-j',type=int,default=2)
    ap.add_argument('--widths',type=int,nargs='+',default=[129,191,192,193,255,256,257,310,500,1024])
    args=ap.parse_args()
    os.environ['VCC_BIN']=str(pathlib.Path(args.bin_dir).resolve())
    os.environ['VCC_BUILD']=str(args.output.resolve()/'runtime')
    args.output.mkdir(parents=True,exist_ok=True)
    bench=pathlib.Path(__file__).resolve().parent
    def module(name,file):
        spec=importlib.util.spec_from_file_location(name,bench/file)
        m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
    ct=module('ct','vcc-ctests.py')
    x64_vir_context=module('x64_vir_context','vcc-x86.py') if args.target=='x86_64' else None
    (x64_vir_context.rt_obj if x64_vir_context else ct.build_rt)()
    def check(bits):
        wd=args.output/str(bits);wd.mkdir(exist_ok=True)
        main=wd/'main.c';main.write_text(caller(bits));base=x64_vir_context.BASE if x64_vir_context else ct.BASE
        caller_obj=wd/'main.o'
        rc,out,err=ct.sh([ct.CL,*base,'-c',str(main),'-o',str(caller_obj)])
        if rc:return dict(width=bits,result='fail',stage='caller',message=err.decode(errors='replace'))
        runs=[]
        for compiler in (['vcc','clang'] if x64_vir_context else ['vcc']):
            obj=wd/(compiler+'.o');exe=ct.VPP if compiler=='vcc' else ct.CL
            rc,out,err=ct.sh([exe,*base,'-DWIDTH='+str(bits),'-c',str(bench.parent/'src/test/bitint/operations.c'),'-o',str(obj)])
            (wd/(compiler+'.log')).write_bytes(out+err)
            if rc:runs.append(dict(compiler=compiler,stage='compile',exit=rc,message=err.decode(errors='replace')));continue
            counts=wd/(compiler+'-counts.o')
            rc,out,err=ct.sh([exe,*base,'-c',str(bench.parent/'src/test/bitint/counts.c'),'-o',str(counts)])
            (wd/(compiler+'-counts.log')).write_bytes(out+err)
            if rc:runs.append(dict(compiler=compiler,stage='counts',exit=rc,message=err.decode(errors='replace')));continue
            elf=str(wd/(compiler+'.elf'));rc,err=(x64_vir_context.link if x64_vir_context else ct.link)([str(caller_obj),str(obj),str(counts)],elf)
            if not rc:rc,out=(x64_vir_context.run if x64_vir_context else ct.qemu)(elf)
            else:out=err
            runs.append(dict(compiler=compiler,stage='run',exit=rc,output=out))
        return dict(width=bits,result='pass' if all(r['exit']==0 for r in runs) else 'fail',runs=runs)
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.j) as pool:results=list(pool.map(check,args.widths))
    (args.output/'results.json').write_text(json.dumps(results,indent=2)+'\n')
    for r in results:print(r['result'],r['width'],json.dumps(r.get('runs',r)) if r['result']!='pass' else '')
    print('%d/%d widths pass independent integer oracle'%(sum(r['result']=='pass' for r in results),len(results)))
    return any(r['result']!='pass' for r in results)

if __name__=='__main__':raise SystemExit(main())
