#!/usr/bin/env python3
"""Dynamic instruction mix of a speedtest1 build, exactly: a nop goes in front of every
instruction of one class in the compiler's assembly, and instructions retired minus the
unchanged build's is how often that class runs.  Works on any compiler's .s.

  cd DIR                      # holds NAME.s (sqlite3.c compiled with -S) and t.o (speedtest1.c)
  bench/sqlite/mix.py NAME [CLASS...]

  vpp:   build/src/vpp -O2 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_THREADSAFE=0 -I. -S sqlite3.c -o v.s
  clang: clang -O2 ... -S sqlite3.c -o c.s

A class whose nops push a tbz out of range is run in 2, 4, ... parts and summed.  Run one
at a time for totals good to about 0.01 G (macOS: /usr/bin/time -l).

All classes in a minute instead of ten: the unchanged build once, then a process per class
(each count comes out about 0.03 G high when they run side by side, for every compiler alike):
  b=$(MIX_ONLYBASE=1 bench/sqlite/mix.py NAME | tail -1)
  printf '%s\n' 'sp load' 'sp store' ... | MIX_BASE=$b xargs -P 7 -I{} bench/sqlite/mix.py NAME '{}'"""
import re,subprocess,sys
CL='/opt/homebrew/opt/llvm/bin/clang'
src=open(sys.argv[1]+'.s').read().split('\n')
C={
 'sp load': r'(ldr|ldur|ldrs?[bhw]|ldp)\s.*\[(sp|x29)',
 'sp store': r'(str|stur|str[bh]|stp)\s.*\[(sp|x29)',
 'mov reg,reg': r'mov\s+[xw]\d+, [xw]\d+\s*$',
 'mov/fmov const': r'(mov\s+[xw]\d+, #|movz|movn|movk|adrp)',
 'b': r'b\s',
 'cond branch': r'(b\.\w+|cbz|cbnz|tbz|tbnz)\s',
 'call': r'(bl|blr)\s',
 'ret': r'ret\b',
 'load': r'(ldr|ldur|ldrs?[bhw]|ldp)\s(?!.*\[(sp|x29))',
 'store': r'(str|stur|str[bh]|stp)\s(?!.*\[(sp|x29))',
 'cmp/tst': r'(cmp\s|cmn\s|tst\s|subs\s+[xw]zr|ands\s+[xw]zr|ccmp\s|ccmn\s)',
 'extend': r'(sxt[bhw]|uxt[bhw]|ubfm|sbfm|ubfx|sbfx|ubfiz|sbfiz|and\s+[xw]\d+, [xw]\d+, #0xff)\b',
 'csel': r'(csel|csinc|csinv|csneg|cset|csetm|cinc)\s',
 'add/sub': r'(add|sub|adds|subs)\s+[xw](\d+|sp),',
 'frame': r'stp\s+x29, x30',
}
def run(lines,tag):
    tag=tag+'-'+'-'.join(sys.argv[2:3]).replace(' ','_').replace('/','_')
    open(f'mix-{tag}.s','w').write('\n'.join(lines))
    r=subprocess.run([CL,'-c',f'mix-{tag}.s','-o',f'mix-{tag}.o'],capture_output=True,text=True)
    if r.returncode: return None
    subprocess.run([CL,f'mix-{tag}.o','t.o','-o',f'mix-{tag}'],check=True)
    e=subprocess.run(['/usr/bin/time','-l',f'./mix-{tag}','--size','100','--memdb'],capture_output=True,text=True).stderr
    return int(re.search(r'(\d+)\s+instructions retired',e).group(1))
import os
tag=sys.argv[1]
base=int(os.environ['MIX_BASE']) if os.environ.get('MIX_BASE') else run(src,tag)
if not os.environ.get('MIX_BASE'): print(f"{tag}: total {base/1e9:.3f} G")
if os.environ.get('MIX_ONLYBASE'): print(base); sys.exit()
only=sys.argv[2:] 
for name,rx in C.items():
    if only and name not in only: continue
    pat=re.compile(r'^\s+'+rx)
    tot=None
    for k in (1,2,4,8,16):
        tot=0
        for part in range(k):
            out=[];i=0
            for l in src:
                if pat.match(l):
                    if i%k==part: out.append('\tnop')
                    i+=1
                out.append(l)
            n=run(out,tag)
            if n is None: tot=None; break
            tot+=n-base
        if tot is not None: break
    print(f"  {name:15} {'asm failed' if tot is None else f'{tot/1e9:7.3f} G  {tot/base*100:5.1f} %'}",flush=True)
