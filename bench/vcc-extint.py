#!/usr/bin/env python3
"""Legacy ExtInt identity, warning contracts and cross-compiler scalar calls.

Clang rejects ARM widths above128; the 310-bit ARM test instead uses an
ordinary-C caller and a vcc bridge/callee, with Python-generated value bytes.
"""
import argparse, json, pathlib, subprocess
from hostenv import BUILD, llvm


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--bin-dir', default=BUILD)
    p.add_argument('--target', choices=['x86_64-linux-gnu', 'aarch64-linux-gnu'], default='x86_64-linux-gnu')
    p.add_argument('--output', required=True, type=pathlib.Path)
    args = p.parse_args()
    out = args.output.resolve(); out.mkdir(parents=True, exist_ok=True)
    compilers = {'vcc': str(pathlib.Path(args.bin_dir).resolve()/'vpp'), 'clang': llvm('clang')}
    records = []; results = []
    def run(cmd, expected=0):
        q = subprocess.run(list(map(str,cmd)), capture_output=True, text=True, timeout=30)
        records.append(dict(argv=list(map(str,cmd)), returncode=q.returncode, stdout=q.stdout, stderr=q.stderr))
        if q.returncode != expected:
            raise RuntimeError(str(cmd)+'\n'+q.stdout+q.stderr)
        return q
    flags = ['--target='+args.target, '-std=gnu17', '-Wno-deprecated-type']
    runner = ['qemu-aarch64', '-L', '/usr/aarch64-linux-gnu'] if args.target.startswith('aarch64') else []
    try:
        mode = out/'mode.c'; mode.write_text('_ExtInt(6) f(_ExtInt(6) x) { return x; }\n')
        for compiler in compilers.values():
            for options in [['-std=c89'], ['-std=c11','-pedantic'], ['-std=c23','-pedantic-errors'], ['-std=c11','-Wno-deprecated-type'], ['-std=gnu17','-Werror=deprecated-type']]:
                q=run([compiler,'--target='+args.target,*options,'-fsyntax-only',mode], int('-Werror=deprecated-type' in options))
                if '-Wno-deprecated-type' in options:
                    assert not q.stderr, q.stderr
                else:
                    assert 'deprecated-type' in q.stderr, q.stderr
        for bits in [6,14,29,58,120,310]:
            wd=out/str(bits);wd.mkdir(exist_ok=True)
            common='''typedef _ExtInt(WIDTH) S;
typedef unsigned _ExtInt(WIDTH) U;
_Static_assert(__builtin_types_compatible_p(S,_BitInt(WIDTH)), "signed identity");
_Static_assert(__builtin_types_compatible_p(U,unsigned _BitInt(WIDTH)), "unsigned identity");
S add(S,S); U flip(U);
'''.replace('WIDTH',str(bits))
            callee=wd/'callee.c';callee.write_text(common+'S add(S a,S b){return a+b;}\nU flip(U x){return x^((U)1<<'+str(bits-1)+');}\n')
            caller=wd/'caller.c';caller.write_text(common+'''int main(void){
 S x=(S)1<<SHIFT;
 if(add(x,-3)!=x-3 || add(-7,2)!=-5)return 1;
 if(flip(11)!=(((U)1<<TOP)|11))return 2;
 return 0;
}
'''.replace('SHIFT',str(bits-2)).replace('TOP',str(bits-1)))
            if bits==310 and runner:
                # Neither claim a Clang-wide oracle nor bypass its rejection.
                q=run([compilers['clang'],*flags,'-fsyntax-only',callee],1)
                assert 'not supported' in q.stderr, q.stderr
                bridge=wd/'bridge.c';bridge.write_text(common+'''void bridge(unsigned char *out){
 U result=flip((U)11); __builtin_memcpy(out,&result,sizeof result);
}
''')
                expected=((1<<(bits-1))|11).to_bytes((bits+7)//8,'little')
                caller.write_text('void bridge(unsigned char*);\nint main(void){unsigned char out[40];static const unsigned char want[]={'+','.join(map(str,expected))+'};bridge(out);for(unsigned i=0;i<39;i++){unsigned mask=i==38?63:255;if((out[i]&mask)!=want[i])return 1;}return 0;}\n')
                objs=[]
                for compiler,src in [('clang',caller),('vcc',bridge),('vcc',callee)]:
                    obj=wd/(src.stem+'.o');run([compilers[compiler],*flags,'-c',src,'-o',obj]);objs.append(obj)
                exe=wd/'independent.exe';run([compilers['clang'],'--target='+args.target,'-fuse-ld=lld','-no-pie',*objs,'-o',exe]);run([*runner,exe])
                results.append(dict(width=bits,kind='Python bytes / ordinary Clang caller / vcc bridge and callee',status='pass',clang_wide='explicit reference rejection'))
                continue
            objects={}
            for half,src in [('caller',caller),('callee',callee)]:
                for name,compiler in compilers.items():
                    obj=wd/(half+'-'+name+'.o');run([compiler,*flags,'-c',src,'-o',obj]);objects[half,name]=obj
            for caller_name in compilers:
                for callee_name in compilers:
                    exe=wd/(caller_name+'-'+callee_name+'.exe')
                    run([compilers['clang'],'--target='+args.target,'-fuse-ld=lld','-no-pie',objects['caller',caller_name],objects['callee',callee_name],'-o',exe]);run([*runner,exe])
                    results.append(dict(width=bits,caller=caller_name,callee=callee_name,status='pass'))
        print(f'{len(results)} runtime combinations pass; identity and warning modes pass')
    finally:
        (out/'results.json').write_text(json.dumps(dict(target=args.target,results=results,commands=records),indent=2)+'\n')


if __name__ == '__main__':
    main()
