#!/usr/bin/env python3
"""Runtime wide/FP casts: exact Python integer IEEE oracle, Clang x86 cross-check.
ARM's Clang-built caller uses only pointer/scalar ABI; no unsupported wide type.
"""
import argparse,json,pathlib,random,subprocess,platform
from hostenv import llvm

def encode(value,p,bias,eb,x87):
    sign=value<0;value=abs(value)
    if not value:return 0
    e=value.bit_length()-1
    shift=e-p
    sig=value<<(-shift) if shift<0 else value>>shift
    if shift>0:
        rem=value-(sig<<shift);half=1<<(shift-1)
        sig+=rem>half or (rem==half and sig&1)
    if sig>>(p+1):sig>>=1;e+=1
    if e+bias >= 2*bias+1:e=2*bias+1;sig=(1<<p) if x87 else 0
    else:e+=bias
    return (sign<<(79 if x87 else p+eb)) | (e<<(64 if x87 else p)) | (sig if x87 else sig&((1<<p)-1))

def decode(raw,p,bias,eb,x87):
    e=(raw>>(64 if x87 else p))&((1<<eb)-1)
    sig=raw&((1<<(p+1 if x87 else p))-1)
    if not x87 and e:sig|=1<<p
    power=(e-bias if e else 1-bias)-p
    value=sig<<power if power>=0 else sig>>(-power)
    return -value if raw>>(79 if x87 else p+eb)&1 else value

def caller(width,p,bias,eb,x87):
    n=(width+63)//64;mask=(1<<width)-1;rng=random.Random(width+p)
    values={0,1,2,3,(1<<width)-1,1<<(width-1),(1<<(width-1))-1}
    for e in {p,p+1,p+2,64,100,128,width-2,width-1}:
        if e<width:
            for d in [-1,0,1]:
                for odd in [0,1]:
                    v=(1<<e)+(odd<<(max(0,e-p)))+(1<<max(0,e-p-1))+d
                    if v<=mask:values.add(v)
    values.update(rng.getrandbits(width) for _ in range(12))
    rows=[]
    for value in sorted(values):
        for sign in [0,1]:
            signed=value-(1<<width) if sign and value>>(width-1) else value
            rows.append((value,sign,encode(signed,p,bias,eb,x87)))
    inverse=[]
    rawvalues={0,1,(1<<p)-1,encode(1,p,bias,eb,x87),encode(3,p,bias,eb,x87)}
    for e in {1,bias-2,bias-1,bias,bias+1,bias+63,bias+127,bias+128,bias+192,bias+309,bias+499,bias+width-3,bias+width-2,bias+width-1,(1<<eb)-2}:
        if e<(1<<eb)-1:
            for frac in [0,1,(1<<p)-1]:rawvalues.add((e<<(64 if x87 else p))|frac|((1<<p) if x87 else 0))
    for raw in sorted(rawvalues):
        for neg in [0,1]:
            raw1=raw|(neg<<(79 if x87 else p+eb));v=decode(raw1,p,bias,eb,x87)
            for sign in [0,1]:
                if (-(1<<(width-1))<=v<(1<<(width-1)) if sign else 0<=v<=mask):inverse.append((raw1,sign,v&mask))
    def words(v,num):return '{'+','.join('0x%xULL'%((v>>(64*i))&((1<<64)-1)) for i in range(num))+'}'
    def table(rows,ni,no):return ',\n'.join('{%s,%d,%s}'%(words(v,ni),s,words(w,no)) for v,s,w in rows)
    fpbits=80 if x87 else 1+eb+p
    return '''#include <stdio.h>
extern unsigned wide_to_float(const unsigned long long*,unsigned,unsigned long long*);
extern void constant_to_float(unsigned long long*);
extern void constant_from_float(unsigned long long*);
extern unsigned float_to_wide(const unsigned long long*,unsigned,unsigned long long*);
#define N %d
static const struct {unsigned long long in[N];unsigned sign;unsigned long long want[2];} forward[]={%s};
static const struct {unsigned long long in[2];unsigned sign;unsigned long long want[N];} backward[]={%s};
int main(void){unsigned long long out[N+2]={0};
static const unsigned long long const_fp[2]=%s,const_int[N]=%s;
constant_to_float(out);
for(unsigned j=0;j<%d;j++){unsigned long long mask=j==%d?0x%xULL:~0ULL;if((out[j]&mask)!=const_fp[j])return 5;}
constant_from_float(out);
for(unsigned j=0;j<N;j++){unsigned long long mask=j==N-1?0x%xULL:~0ULL;if((out[j]&mask)!=const_int[j])return 6;}
for(unsigned i=0;i<sizeof forward/sizeof *forward;i++){
 if(wide_to_float(forward[i].in,forward[i].sign,out)!=1)return 3;
 for(unsigned j=0;j<%d;j++){unsigned long long mask=j==%d?0x%xULL:~0ULL;
 if((out[j]&mask)!=forward[i].want[j]){printf("forward %%u limb %%u: %%llx expected %%llx\\n",i,j,out[j]&mask,forward[i].want[j]);return 1;}}}
for(unsigned i=0;i<sizeof backward/sizeof *backward;i++){
 if(float_to_wide(backward[i].in,backward[i].sign,out)!=1)return 4;
 for(unsigned j=0;j<N;j++){unsigned long long mask=j==N-1?0x%xULL:~0ULL;
 if((out[j]&mask)!=backward[i].want[j]){printf("backward %%u limb %%u: %%llx expected %%llx\\n",i,j,out[j]&mask,backward[i].want[j]);return 2;}}}return 0;}
'''%(n,table(rows,n,2),table(inverse,2,n),words(encode(1<<(width-2),p,bias,eb,x87),2),words(3<<99,n),(fpbits+63)//64,(fpbits-1)//64,(1<<((fpbits-1)%64+1))-1,mask>>(64*(n-1)),(fpbits+63)//64,(fpbits-1)//64,(1<<((fpbits-1)%64+1))-1,mask>>(64*(n-1))),len(rows)+len(inverse)+2

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--bin-dir',required=True);ap.add_argument('--output',type=pathlib.Path,required=True);ap.add_argument('--target',choices=['x86_64-linux-gnu','aarch64-linux-gnu'],default='x86_64-linux-gnu');ap.add_argument('--widths',nargs='+',type=int,default=[129,193,310,500,7001]);ap.add_argument('--cflag',action='append',default=[],help='extra compiler option, e.g. --cflag=-O3');args=ap.parse_args()
    args.output.mkdir(parents=True,exist_ok=True);commands=[];differences=[];cl=llvm('clang');target='--target='+args.target;x86=args.target.startswith('x86');totals={'vcc':0,'clang':0}
    def run(cmd,oracle=False):
        q=subprocess.run(list(map(str,cmd)),capture_output=True,text=True,timeout=120);commands.append(dict(argv=list(map(str,cmd)),exit=q.returncode,stdout=q.stdout,stderr=q.stderr))
        if q.returncode:
            if oracle and q.returncode == 1 and q.stdout.startswith('forward '):
                differences.append(dict(argv=list(map(str,cmd)),exit=q.returncode,stdout=q.stdout,stderr=q.stderr));return False
            raise RuntimeError(str(cmd)+'\n'+q.stdout+q.stderr)
        return True
    formats=[('_Float16',10,15,5,False),('__bf16',7,127,8,False),('float',23,127,8,False),('double',52,1023,11,False),('long double',63 if x86 else 112,16383,15,x86),('__float128',112,16383,15,False)]
    # Clang 23 converts wide integers via float then __truncsfbf2, which
    # double-rounds (2**64 + 2**56 + 1 -> 0x5f80 instead of 0x5f81).
    # x87 similarly goes via binary128 then __trunctfxf2: 2**127 +
    # 2**63 + 1 loses its sticky bit. Clang also incorrectly makes
    # unsigned _BitInt(129) 2**128 negative when converting to __float128.
    # Record these oracle disagreements explicitly; vcc must match Python
    # for every case and format, with no exemptions.
    if not x86:formats=[f for f in formats if f[0]!='__float128']
    native = platform.machine().lower() in (('x86_64','amd64') if x86 else ('aarch64','arm64'))
    runner = [] if native else (['qemu-x86_64','-L','/usr/x86_64-linux-gnu'] if x86 else ['qemu-aarch64','-L','/usr/aarch64-linux-gnu'])
    fixture=pathlib.Path(__file__).resolve().parent.parent/'src/test/bitint/float-runtime.c'
    try:
        for width in args.widths:
            for fp,p,bias,eb,x87 in formats:
                wd=args.output/(str(width)+'-'+fp.replace(' ','_'));wd.mkdir(exist_ok=True);source,count=caller(width,p,bias,eb,x87);main=wd/'main.c';main.write_text(source)
                for name,compiler in [('vcc',str(pathlib.Path(args.bin_dir).resolve()/'vpp'))]+([('clang',cl)] if x86 else []):
                    asm=wd/(name+'.s');exe=wd/(name+'.exe')
                    run([compiler,target,'-std=c23',*args.cflag,'-DWIDTH='+str(width),'-DFP='+fp,'-S',fixture,'-o',asm])
                    run([cl,target,'-fuse-ld=lld',main,asm,'-o',exe])
                    ok=run([*runner,str(exe)],oracle=name=='clang' and fp in ('__bf16','long double','__float128'))
                    print(width,fp,name,count,'PASS' if ok else 'ORACLE DISAGREEMENT (see commands.json)',flush=True)
                    if ok:totals[name]+=count
        if not x86:
            unsupported=args.output/'unsupported-float128.c'
            unsupported.write_text('__float128 f(unsigned _BitInt(310) x) { return x; }\n')
            cmd=[str(pathlib.Path(args.bin_dir).resolve()/'vpp'),target,'-fsyntax-only',str(unsupported)]
            q=subprocess.run(cmd,capture_output=True,text=True,timeout=30)
            commands.append(dict(argv=cmd,exit=q.returncode,stdout=q.stdout,stderr=q.stderr))
            if q.returncode<=0 or '__float128 is not supported on this target' not in q.stderr:
                raise RuntimeError('missing explicit ARM __float128 diagnosis: '+q.stderr)
        print(totals['vcc'],'vcc exact conversion checks passed;',totals['clang'],'Clang cross-checks passed;',len(differences),'Clang oracle disagreements recorded')
    finally:(args.output/'commands.json').write_text(json.dumps(commands,indent=2))
if __name__=='__main__':main()
