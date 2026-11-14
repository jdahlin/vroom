#!/usr/bin/env python3
"""vlink-pe-bad.py: corrupt COFF objects, archives and import records fail cleanly in vlink's PE linker.

  bench/vlink-pe-bad.py [--bin-dir DIR] [-n N]     (default: the ASan+UBSan build, make -C src vcc-asan)

A good COFF x86-64 object (functions, data, BSS, a COMDAT with an associative
.pdata/.xdata, relocations of several types, a weak external, a .drectve), a
GNU archive and a Microsoft-format archive holding it, and a short import
record are corrupted one field at a time, then N times at random bytes (a
fixed seed); vlink -m i386pep must exit 1 with an "error:" line or link, never
die by a signal or a sanitizer report.  The unchanged inputs must link.
"""
import argparse, os, random, struct, subprocess, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from hostenv import BUILD, llvm

SRC = r'''
int d = 3, *p = &d;
int bss_v;
__declspec(selectany) int sel = 4;
static int s(int x) { return x + d + sel; }
__attribute__((weak)) int w(void) { return 1; }
__declspec(dllexport) int start(void) { return s(*p) + w() + bss_v; }
'''

def put(b, off, fmt, v):
    b = bytearray(b)
    struct.pack_into(fmt, b, off, v)
    return bytes(b)

def u(fmt, b, off):
    return struct.unpack_from(fmt, b, off)[0]

def obj_cases(o):
    nsec, symptr, nsym = u('<H', o, 2), u('<I', o, 8), u('<I', o, 12)
    yield 'truncated', o[:len(o) // 2]
    yield 'header-only', o[:20]
    yield 'nsections-huge', put(o, 2, '<H', 0xfffe)
    yield 'symptr-past-end', put(o, 8, '<I', len(o) + 100)
    yield 'nsym-huge', put(o, 12, '<I', 0x7fffffff)
    yield 'nsym-wrap', put(o, 12, '<I', 0xffffffff)
    yield 'opthdr-huge', put(o, 16, '<H', 0xffff)
    strtab = symptr + 18 * nsym
    yield 'strtab-size-huge', put(o, strtab, '<I', 0xfffffff0)
    yield 'strtab-size-small', put(o, strtab, '<I', 2)
    for k in range(nsec):
        h = 20 + 40 * k
        yield f'sec{k}-rawptr', put(o, h + 20, '<I', len(o) + 4096)
        yield f'sec{k}-rawsize', put(o, h + 16, '<I', 0x7ffffff0)
        yield f'sec{k}-relptr', put(o, h + 24, '<I', len(o) - 2)
        yield f'sec{k}-nreloc', put(o, h + 32, '<H', 0xffff)
        yield f'sec{k}-nreloc-ovfl', put(put(o, h + 32, '<H', 0xffff), h + 36, '<I', u('<I', o, h + 36) | 0x01000000)
        yield f'sec{k}-longname', put(o, h, '8s', b'/9999999')
        yield f'sec{k}-longname-bad', put(o, h, '8s', b'/x1')
        yield f'sec{k}-base64name', put(o, h, '8s', b'//AAAAAA')
        yield f'sec{k}-align', put(o, h + 36, '<I', u('<I', o, h + 36) | 0x00f00000)
        nrel, relptr = u('<H', o, h + 32), u('<I', o, h + 24)
        for r in range(min(nrel, 3)):
            e = relptr + 10 * r
            yield f'sec{k}-rel{r}-symidx', put(o, e + 4, '<I', nsym + 7)
            yield f'sec{k}-rel{r}-offset', put(o, e, '<I', 0xfffffff0)
            yield f'sec{k}-rel{r}-type', put(o, e + 8, '<H', 0x7f)
            yield f'sec{k}-rel{r}-at-end', put(o, e, '<I', max(u('<I', o, h + 16) - 2, 0))
    i = 0
    while i < nsym:
        e = symptr + 18 * i
        naux = o[e + 17]
        yield f'sym{i}-section', put(o, e + 12, '<h', nsec + 3)
        yield f'sym{i}-section-neg', put(o, e + 12, '<h', -5)
        yield f'sym{i}-naux', put(o, e + 17, 'B', 0xff)
        yield f'sym{i}-name', put(put(o, e, '<I', 0), e + 4, '<I', 0x7ffffff0)
        if naux and o[e + 16] == 3:
            yield f'sym{i}-assoc', put(o, e + 18 + 12, '<H', 0x7fff)
            yield f'sym{i}-select', put(o, e + 18 + 14, 'B', 9)
        if o[e + 16] == 105:
            yield f'sym{i}-weak-tag', put(o, e + 18, '<I', nsym + 9)
        i += 1 + naux

def archive_cases(a):
    yield 'ar-truncated', a[:len(a) // 2]
    yield 'ar-magic-only', a[:8]
    yield 'ar-member-size', a[:8 + 48] + b'9999999999' + a[8 + 58:]
    yield 'ar-member-size-garbage', a[:8 + 48] + b'12x4567890' + a[8 + 58:]
    yield 'ar-fmag', a[:8 + 58] + b'xx' + a[8 + 60:]
    # the first member is the symbol table: corrupt its counts and offsets
    yield 'ar-symtab-count', a[:68] + b'\xff\xff\xff\x7f' + a[72:]
    yield 'ar-symtab-offset', a[:72] + b'\x7f\xff\xff\xf0' + a[76:]
    yield 'ar-longname', a.replace(b'//              ', b'//              ', 1).replace(b'/0  ', b'/999', 1)

def import_cases(m):
    yield 'imp-truncated', m[:10]
    yield 'imp-sizeofdata', put(m, 12, '<I', 0x7fffffff)
    yield 'imp-no-nul', m[:20] + m[20:].replace(b'\0', b'x')
    yield 'imp-nametype', put(m, 18, '<H', 7 << 2)
    yield 'imp-machine', put(m, 6, '<H', 0x14c)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--bin-dir', default=os.environ.get('VCC_BIN') or f'{BUILD}-asan')
    ap.add_argument('-n', type=int, default=300)
    args = ap.parse_args()
    vlink = os.path.join(args.bin_dir, 'vlink')
    if not os.path.exists(vlink):
        sys.exit(f'vlink-pe-bad: no {vlink} (make -C src vcc-asan)')
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0:abort_on_error=0', UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    tmp = tempfile.mkdtemp(prefix='vlink-pe-bad-')
    open(f'{tmp}/x.c', 'w').write(SRC)
    subprocess.run([llvm('clang'), '--target=x86_64-w64-mingw32', '-O1', '-c', f'{tmp}/x.c', '-o', f'{tmp}/x.o'], check=True)
    obj = open(f'{tmp}/x.o', 'rb').read()
    subprocess.run([llvm('llvm-ar'), 'rcs', '--format=gnu', f'{tmp}/g.a', f'{tmp}/x.o'], check=True)
    subprocess.run([llvm('llvm-ar'), 'rcs', '--format=coff', f'{tmp}/c.a', f'{tmp}/x.o'], check=True)
    open(f'{tmp}/d.def', 'w').write('LIBRARY d.dll\nEXPORTS\nfoo\nbar DATA\n')
    subprocess.run([llvm('llvm-dlltool'), '-m', 'i386:x86-64', '-d', f'{tmp}/d.def', '-l', f'{tmp}/d.a'], check=True)
    imp = None
    d = open(f'{tmp}/d.a', 'rb').read()
    o = 8
    while o + 60 <= len(d):
        n = int(d[o + 48:o + 58])
        if d[o + 60:o + 64] == b'\0\0\xff\xff':
            imp = d[o + 60:o + 60 + n]
            break
        o += 60 + n + (n & 1)
    base = ['-m', 'i386pep', '--entry', 'start', '--subsystem', 'console', '-u', 'start', '--require-defined', 'start']
    def link(files, name):
        p = subprocess.run([vlink] + base + files + ['-o', f'{tmp}/out.exe'], capture_output=True, text=True, errors='replace', env=env, timeout=60)
        bad = p.returncode < 0 or 'Sanitizer' in p.stderr or 'runtime error' in p.stderr or \
            (p.returncode and 'error:' not in p.stderr) or p.returncode not in (0, 1)
        return bad, p
    fails = 0
    total = 0
    for files in ([f'{tmp}/x.o'], [f'{tmp}/g.a', '-u', 'start'], [f'{tmp}/c.a', '-u', 'start']):
        bad, p = link(files, 'good')
        if bad or p.returncode:
            print(f'FAIL good input {files[0]}: rc {p.returncode}\n{p.stderr[:800]}')
            fails += 1
    cases = [('obj', n, b) for n, b in obj_cases(obj)]
    ga = open(f'{tmp}/g.a', 'rb').read()
    ca = open(f'{tmp}/c.a', 'rb').read()
    cases += [('gnu-ar', n, b) for n, b in archive_cases(ga)]
    cases += [('coff-ar', n, b) for n, b in archive_cases(ca)]
    if imp:
        cases += [('import', n, b) for n, b in import_cases(imp)]
    rnd = random.Random(1)
    for k in range(args.n):
        src = rnd.choice([obj, ga, ca, imp or obj])
        b = bytearray(src)
        for _ in range(rnd.randint(1, 8)):
            i = rnd.randrange(len(b))
            b[i] = rnd.randrange(256)
        cases.append(('random', f'r{k}', bytes(b)))
    for kind, name, data in cases:
        total += 1
        ext = '.o' if kind in ('obj', 'random') and data[:2] != b'!<' else '.a' if data[:2] == b'!<' else '.o'
        if kind == 'import':
            # a short import record alone, then inside an archive
            path = f'{tmp}/bad.o'
        else:
            path = f'{tmp}/bad{ext}'
        open(path, 'wb').write(data)
        files = [path, f'{tmp}/x.o'] if kind == 'import' else [path, '-u', 'start'] if ext == '.a' else [path]
        try:
            bad, p = link(files, name)
        except subprocess.TimeoutExpired:
            print(f'FAIL {kind} {name}: timeout')
            fails += 1
            continue
        if bad:
            fails += 1
            print(f'FAIL {kind} {name}: rc {p.returncode}\n' + '\n'.join(p.stderr.splitlines()[:12]))
    print(f'vlink-pe-bad: {total - fails}/{total} ok')
    sys.exit(1 if fails else 0)

if __name__ == '__main__':
    main()
