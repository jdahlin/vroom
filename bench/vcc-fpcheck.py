#!/usr/bin/env python3
"""No FP/SIMD instructions generated for code compiled with -mgeneral-regs-only.

  vcc-fpcheck.py [PAT] [-j N]   assembly goes to $OBJ/fp (default $TMPDIR/vcc-obj)

The kernel builds almost everything with -mgeneral-regs-only: FP/SIMD is off
at EL1 outside kernel_neon_begin()/end(), so one such instruction traps.
(26c2d50 saved v0-v7 in every variadic prologue: the first printk trapped
before any console existed and the kernel hung silently.)  vcc-obj.py
compares symbols and relocations, not instructions, so it cannot see this.

Compiles every kernel C TU whose flags include -mgeneral-regs-only with
vpp --batch -S (the instruction stream is the one -c encodes) and scans the
text outside //APP ... //NO_APP (inline asm, which the kernel may use inside
kernel_neon_begin): an instruction fails if it names a b/h/s/d/q/v/z register
or is an FP mnemonic; a .inst word fails if bits 27:26 are 11 (every A64
FP/SIMD encoding, data processing and loads/stores alike).
"""
import os, re, subprocess, sys
import importlib.util
spec = importlib.util.spec_from_file_location('vo', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'vcc-obj.py'))
vo = importlib.util.module_from_spec(spec); spec.loader.exec_module(vo)

FPREG = re.compile(r'(?<![\w.$])[bhsdqvz](?:[0-9]|[12][0-9]|3[01])(?:\.\w+)?(?:\[\d+\])?(?![\w$])')
FPOP = re.compile(r'(?:f\w+|[su]cvtf|ld\d|st\d|ld\dr)$')
BRANCH = {'b', 'bl', 'adr', 'adrp', 'cbz', 'cbnz', 'tbz', 'tbnz'}


def scan(path):
    """[(function, line)] of FP/SIMD instructions outside inline asm"""
    out, fn, app = [], '?', False
    for ln in open(path, errors='replace'):
        s = ln.strip()
        if s == '//APP':
            app = True
            continue
        if s == '//NO_APP':
            app = False
            continue
        if app or not s or s.startswith('//'):
            continue
        if re.fullmatch(r'[\w.$]+:', s):
            if not s.startswith('.L') and not s[0].isdigit():
                fn = s[:-1]
            continue
        m, _, ops = s.partition('\t') if '\t' in s else s.partition(' ')
        if m == '.inst':
            if any(re.fullmatch(r'\s*(0x[0-9a-fA-F]+|\d+)\s*', w) and int(w, 0) & 0x0C000000 == 0x0C000000
                   for w in ops.split(',')):
                out.append((fn, s))
        elif m.startswith('.') or m.split('.')[0] in BRANCH:
            continue
        elif FPOP.match(m.split('.')[0]) or FPREG.search(ops.split('//')[0]):
            out.append((fn, s))
    return out


def main():
    av = sys.argv[1:]
    j = 6
    if '-j' in av:
        k = av.index('-j'); j = int(av[k + 1]); del av[k:k + 2]
    D = f'{vo.OBJ}/fp'
    os.makedirs(D, exist_ok=True)
    tus = [(o, a) for o, a in vo.c_tus(av[0] if av else None) if '-mgeneral-regs-only' in a]
    jf = f'{D}/jobs'
    out = lambda obj: f'{D}/{obj.replace("/", "__")}.s'
    with open(jf, 'w') as f:
        for obj, a in tus:
            f.write('\x1f'.join(['-'] + a + ['-S', '-o', out(obj)]) + '\n')
    for obj, _ in tus:
        if os.path.exists(out(obj)):
            os.unlink(out(obj))
    r = subprocess.run([vo.VPP, '--batch', jf, '-j', str(j), '-C', vo.B], capture_output=True, text=True)
    if r.returncode:
        print(r.stderr.strip()[-2000:])
    bad = 0
    for obj, _ in tus:
        if not os.path.exists(out(obj)):
            bad += 1
            print(f'{obj}: not compiled')
            continue
        hits = scan(out(obj))
        if hits:
            bad += 1
            if bad <= 20:
                fns = {}
                for fn, s in hits:
                    fns.setdefault(fn, []).append(s)
                print(f'{obj}: ' + '; '.join(f'{fn}: {len(v)}, e.g. "{v[0]}"' for fn, v in list(fns.items())[:3])
                      + (' ...' if len(fns) > 3 else ''))
    print(f'{len(tus) - bad}/{len(tus)} -mgeneral-regs-only TUs without generated FP/SIMD code')
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
