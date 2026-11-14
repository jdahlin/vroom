#!/usr/bin/env python3
"""vas-obj.py: whole-object parity of vasm with clang -c on small .s files.

  bench/vas-obj.py [DIR...]     default: src/test/vas-obj

Each file is assembled by clang (--target=aarch64-linux-gnu; x86-*.s files
--target=x86_64-linux-gnu, vasm --target=x86_64; win64-*.s files
--target=x86_64-w64-mingw32, COFF objects, both sides) and by vasm;
the objects must be byte-identical.  On a mismatch llvm-readelf shows the
difference.  A file clang rejects must fail in vasm too, with clang's first
errors at the same file:lines.  Directives, symbol tables and relocations, <1 s.
Each file that assembles is also assembled with -g on both sides (MC's debug
information for an assembler source; vasm -g -dwarf-debug-producer as clang's).
"""
import glob, os, subprocess, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from hostenv import llvm, BUILD
R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VAS = os.environ.get('VAS', f'{BUILD}/vasm')
CL = llvm('clang'); RE = llvm('llvm-readelf'); RO = llvm('llvm-readobj')
# -g differences known, with their TODO.md item (an XPASS is an error)
G_XFAIL = {'symver.s': 'TODO.md WIN6: .symver names are in the symbol table before the -g sections\' symbols (MC: after)'}
PRODUCER = subprocess.run([CL, '--version'], capture_output=True, text=True).stdout.splitlines()[0]	# (cc1as's -dwarf-debug-producer)

def main():
    dirs = sys.argv[1:] or [f'{R}/src/test/vas-obj']
    files = sorted(f for d in dirs for f in glob.glob(f'{d}/*.s'))
    ok = bad = gok = gbad = gxfail = 0
    with tempfile.TemporaryDirectory() as t:
        for f in files:
            c, v = f'{t}/c.o', f'{t}/v.o'
            x86 = os.path.basename(f).startswith('x86-')
            win = os.path.basename(f).startswith('win64-')
            tgt = '--target=x86_64-w64-mingw32' if win else '--target=x86_64-linux-gnu' if x86 else '--target=aarch64-linux-gnu'
            p = subprocess.run([CL, tgt, '-c', f, '-o', c], capture_output=True, text=True)
            q = subprocess.run([VAS] + ([tgt] if win else ['--target=x86_64'] if x86 else []) + [f, '-o', v], capture_output=True, text=True)
            if p.returncode:    # an error test: the same file:line of every error
                import re
                ce = re.findall(r'^(\S+?):(\d+):\d+: (?:error|note)', p.stderr, re.M)
                ve = re.findall(r'^(\S+?):(\d+): (?:error|note)', q.stderr, re.M)
                if q.returncode and ce and ce == ve: ok += 1
                else:
                    bad += 1
                    print('DIFF', os.path.basename(f), 'clang:', ce, 'vasm:', ve or 'no error')
                continue
            if q.returncode or open(c, 'rb').read() != open(v, 'rb').read():
                bad += 1
                print('DIFF', os.path.basename(f), q.stderr.strip()[:300])
                if not p.returncode and not q.returncode:
                    show = [RO, '--file-headers', '--sections', '--symbols', '--relocs', '--unwind'] if win else [RE, '-a']
                    a = subprocess.run(show + [c], capture_output=True, text=True).stdout.splitlines()
                    b = subprocess.run(show + [v], capture_output=True, text=True).stdout.splitlines()
                    import difflib
                    for l in list(difflib.unified_diff(a, b, 'clang', 'vasm', lineterm='', n=0))[:30]: print('  ' + l)
            else: ok += 1
            # -g on the source (MC's debug information for assembler sources; .file N turns it off)
            pg = subprocess.run([CL, tgt, '-g', '-c', f, '-o', c], capture_output=True, text=True)
            qg = subprocess.run([VAS] + ([tgt] if win else ['--target=x86_64'] if x86 else []) + ['-g', '-dwarf-debug-producer', PRODUCER, f, '-o', v],
                                capture_output=True, text=True)
            same = not pg.returncode and not qg.returncode and open(c, 'rb').read() == open(v, 'rb').read()
            known = G_XFAIL.get(os.path.basename(f))
            if known and same:
                gbad += 1
                print('XPASS -g', os.path.basename(f), '(remove it from G_XFAIL)')
            elif known: gxfail += 1
            elif not same:
                gbad += 1
                print('DIFF -g', os.path.basename(f), (qg.stderr or pg.stderr).strip()[:200])
            else: gok += 1
    print(f'vas-obj: {ok}/{ok + bad} identical, -g {gok}/{gok + gbad}' + (f' ({gxfail} xfail)' if gxfail else ''))
    sys.exit(1 if bad or gbad else 0)

if __name__ == '__main__': main()
