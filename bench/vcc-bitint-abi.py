#!/usr/bin/env python3
"""Wide BitInt transport: Clang cross-calls on x86; explicit AAPCS aggregate
proxy cross-calls on ARM, where Clang23 rejects BitInt widths above128.
Uses the existing native/bare-metal runtime and link/run helpers.
"""
import argparse, concurrent.futures, importlib.util, json, os, pathlib

ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('--target', choices=['x86_64', 'aarch64'], default='x86_64')
ap.add_argument('--bin-dir', required=True)
ap.add_argument('--output', type=pathlib.Path, required=True)
ap.add_argument('-j', type=int, default=6)
a = ap.parse_args()
os.environ['VCC_BIN'] = str(pathlib.Path(a.bin_dir).resolve())
os.environ['VCC_BUILD'] = str(a.output.resolve() / 'runtime')
a.output.mkdir(parents=True, exist_ok=True)
bench = pathlib.Path(__file__).resolve().parent
source = bench.parent / 'src/test/bitint-abi'

def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, bench / filename)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m

ct = module('ct', 'vcc-ctests.py')
x64_vir_context = module('x64_vir_context', 'vcc-x86.py') if a.target == 'x86_64' else None
if x64_vir_context:
    x64_vir_context.rt_obj()
else:
    ct.build_rt()

def check(shape):
    width, signed, align = shape
    wd = a.output / (str(width)+('-signed' if signed else '-unsigned')+'-align'+str(align))
    wd.mkdir(exist_ok=True)
    objects = {}
    base = x64_vir_context.BASE if x64_vir_context else ct.BASE
    for half in ['caller', 'callee']:
        for compiler in ['clang', 'vcc']:
            obj = str(wd / f'{half}-{compiler}.o')
            flags = [*base, '-DWIDTH='+str(width), '-DSIGNED='+str(signed), '-DTYPE_ALIGN='+str(align), '-I', str(source)]
            if not x64_vir_context and compiler == 'clang':
                flags += ['-DABI_PROXY=1']
            exe = ct.CL if compiler == 'clang' else ct.VPP
            cmd = [exe, *flags, '-c', str(source / (half+'.c')), '-o', obj]
            rc, out, err = ct.sh(cmd)
            (wd / f'{half}-{compiler}.log').write_bytes(out+err)
            if rc:
                return dict(width=width, signed=signed, align=align, result='fail', stage=half+'-'+compiler,
                            message=err.decode(errors='replace'))
            objects[half,compiler] = obj
    directions = []
    for caller in ['clang','vcc']:
        for callee in ['clang','vcc']:
            name = caller+'-'+callee
            elf = str(wd / (name+'.elf'))
            rc, err = (x64_vir_context.link if x64_vir_context else ct.link)([objects['caller',caller],objects['callee',callee]],elf)
            if not rc:
                rc, out = (x64_vir_context.run if x64_vir_context else ct.qemu)(elf)
            else:
                out = err
            directions.append(dict(caller=caller,callee=callee,exit=rc,output=out))
    return dict(width=width,signed=signed,align=align,result='pass' if all(d['exit']==0 for d in directions) else 'fail',directions=directions)

# Include partial storage words, limb boundaries and the unchanged Kefir500 shape.
widths = [129,130,191,192,193,255,256,257,500,512,513]
with concurrent.futures.ThreadPoolExecutor(max_workers=a.j) as pool:
    results = list(pool.map(check,[(w,s,8) for w in widths for s in (0,1)]+[(500,s,32) for s in (0,1)]))
(a.output/'results.json').write_text(json.dumps(results,indent=2)+'\n')
for r in results:
    print(r['result'],r['width'],'signed' if r['signed'] else 'unsigned','align'+str(r['align']),r.get('message',''),flush=True)
mode = 'Clang BitInt ABI' if x64_vir_context else 'documented AAPCS aggregate proxy (NOT Clang BitInt parity)'
print(mode+': '+str(sum(r['result']=='pass' for r in results))+'/'+str(len(results))+' pass; four compiler directions each')
raise SystemExit(any(r['result']!='pass' for r in results))
