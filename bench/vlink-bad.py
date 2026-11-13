#!/usr/bin/env python3
"""vlink-bad.py: corrupt ELF inputs and archives fail cleanly (TODO.md "ELF input
parsing unchecked").

  bench/vlink-bad.py [--bin-dir DIR]     (default build/ccs-asan: ASan+UBSan)

A good object (two functions, data, relocations) and an archive holding it are
corrupted one field at a time; vlink must exit 1 with an "error:" line, no
signal, no sanitizer report; vobjcopy -O binary and vnm must not crash.  The unchanged inputs must link.
The same for a wasm64 object and archive (vlink -mwasm64): a length or count
that wraps, a size past the end of the file.
"""
import argparse, os, struct, subprocess, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from hostenv import R, has_wasm

SRC = r'''
int d = 3, *p = &d;
static int s(int x) { return x + d; }
int _start(void) { return s(*p); }
'''

def u(fmt, b, off): return struct.unpack_from(fmt, b, off)[0]

def shdrs(b):
    shoff, = struct.unpack_from('<Q', b, 0x28)
    n, = struct.unpack_from('<H', b, 0x3c)
    return shoff, n

def sec(b, typ):
    """offset of the first section header of type typ"""
    shoff, n = shdrs(b)
    for k in range(n):
        h = shoff + 64 * k
        if u('<I', b, h + 4) == typ: return h
    raise SystemExit(f'no section of type {typ}')

def put(b, off, fmt, v):
    b = bytearray(b); struct.pack_into(fmt, b, off, v); return bytes(b)

def obj_cases(o):
    shoff, n = shdrs(o)
    sym, rela = sec(o, 2), sec(o, 4)
    symoff, relaoff = u('<Q', o, sym + 24), u('<Q', o, rela + 24)
    nsym = u('<Q', o, sym + 32) // 24
    tgt = shoff + 64 * u('<I', o, rela + 44)
    yield 'truncated', o[:len(o) // 2]
    yield 'header-only', o[:64]
    yield 'e_shoff-past-end', put(o, 0x28, '<Q', len(o) + 64)
    yield 'e_shoff-odd', put(o, 0x28, '<Q', shoff + 1)
    yield 'e_shnum-huge', put(o, 0x3c, '<H', 0xfeff)
    yield 'e_shentsize', put(o, 0x3a, '<H', 40)
    yield 'e_shstrndx', put(o, 0x3e, '<H', n + 3)
    yield 'shnum0-xindex-huge', put(o, 0x3c, '<H', 0)
    yield 'sh_offset-past-end', put(o, sym + 24, '<Q', len(o) * 4)
    yield 'sh_size-huge', put(o, sym + 32, '<Q', 1 << 62)
    yield 'sh_name', put(o, shoff + 64 + 0, '<I', 0x7fffffff)
    yield 'sh_link-strtab', put(o, sym + 40, '<I', n + 1)
    yield 'symtab-sh_info', put(o, sym + 44, '<I', nsym + 5)
    yield 'st_name', put(o, symoff + 24 + 0, '<I', 0x7fffffff)
    yield 'st_shndx-0x500', put(o, symoff + 24 * (nsym - 1) + 6, '<H', 0x500)
    yield 'st_shndx-xindex', put(o, symoff + 24 * (nsym - 1) + 6, '<H', 0xffff)
    yield 'rela-sh_info', put(o, rela + 44, '<I', n + 2)
    yield 'r_sym', put(o, relaoff + 8, '<Q', (nsym + 9) << 32 | 0x113)
    yield 'abs64-at-size-1', put(put(o, relaoff + 8, '<Q', 1 << 32 | 257),
                                 relaoff, '<Q', u('<Q', o, tgt + 32) - 1)
    yield 'r_offset-huge', put(o, relaoff, '<Q', 1 << 40)
    yield 'sh_addralign-3', put(o, tgt + 48, '<Q', 3)
    # section 0 is the file's to describe: e_shstrndx and sh_link may name it
    sh0 = put(put(put(o, shoff + 4, '<I', 3), shoff + 24, '<Q', 0x7fffffff00000000), shoff + 32, '<Q', 1)
    yield 'shstrndx-0-strtab-huge', put(sh0, 0x3e, '<H', 0)
    yield 'symtab-link-0-strtab-huge', put(sh0, sym + 40, '<I', 0)

def hdr(name, n):
    return f'{name:<16}{0:<12}{0:<6}{0:<6}{644:<8}{n:<10}`\n'.encode()

def ar(members):
    """members: (name as in the header, data)"""
    out = b'!<arch>\n'
    for name, data in members:
        out += hdr(name, len(data)) + data
        if len(data) & 1: out += b'\n'
    return out

def ar_cases(o):
    a = ar([('o.o/', o)])
    yield 'ar-member-past-end', a[:8 + 60 + len(o) // 2]
    yield 'ar-bad-magic-fmag', a[:8 + 58] + b'xx' + a[8 + 60:]
    yield 'ar-size-huge', a[:8 + 48] + b'9999999999' + a[8 + 58:]
    yield 'ar-longname-offset', ar([('//', b'xx/\n'), ('/9999', o)])
    yield 'ar-longname-truncated', ar([('//', b'xx/\n')])[:-2]

# ---- wasm64: vlink -mwasm64 (ld/wasmld.c) ----
def uleb(b, j):
    v = s = 0
    while True:
        c = b[j]; j += 1; v |= (c & 0x7f) << s; s += 7
        if not c & 0x80: return v, j

def enc(v):
    out = bytearray()
    while True:
        c = v & 0x7f; v >>= 7
        if v: out.append(c | 0x80)
        else: out.append(c); return bytes(out)

def wasm_linking(o, newlen):
    """o with the length of the first linking subsection replaced by newlen (a 10-byte uleb)"""
    i = 8
    while i < len(o):
        v, j = uleb(o, i + 1)
        if o[i] == 0:
            nl, k = uleb(o, j)
            if o[k:k + nl] == b'linking':
                p = k + nl
                _, p2 = uleb(o, p)
                _, p3 = uleb(o, p2 + 1)
                body = o[j:p2 + 1] + newlen + o[p3:j + v]
                return o[:i + 1] + enc(len(body)) + body + o[j + v:]
        i = j + v
    raise SystemExit('no linking section')

def wasm_cases(o):
    be = lambda n: struct.pack('>I', n)
    names = b'_start\0d\0p\0'
    sd = be(3) + be(0) * 3 + names            # "/" symbol table: count, offsets, names
    mo = 8 + 60 + len(sd) + (len(sd) & 1)
    sd = be(3) + be(mo) * 3 + names
    def arch(symhdr, symdata, member=o):
        pad = lambda d: d + b'\n' * (len(d) & 1)
        return b'!<arch>\n' + hdr(symhdr, len(symdata)) + pad(symdata) + hdr('a.o/', len(member)) + pad(member)
    good = arch('/', sd)
    yield 'good-wasm-obj', o, True
    yield 'good-wasm-ar', good, True
    yield 'wasm-linking-len-2^63', wasm_linking(o, bytes([0x80] * 9 + [1])), False
    yield 'wasm-linking-len-wraps', wasm_linking(o, bytes([0xf8] + [0xff] * 8 + [1])), False
    yield 'wasm-ar-symtab-past-end', good[:8 + 48] + b'9999999999' + good[8 + 58:], False
    yield 'wasm-ar-sym64-count-wraps', arch('/SYM64/', struct.pack('>Q', 1 << 61) + struct.pack('>Q', mo) * 3 + names), False
    yield 'wasm-ar-count-past-member', arch('/', be(1000) + be(mo) + names), False
    yield 'wasm-ar-member-past-end', good[:-(len(o) // 2)], False
    yield 'wasm-ar-member-offset-wraps', arch('/', be(1) + be(0xffffffe0) + names), False

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--bin-dir', default=f'{R}/build/ccs-asan')
    a = ap.parse_args()
    vlink, vpp = f'{a.bin_dir}/vlink', f'{a.bin_dir}/vpp'
    vobjcopy, vnm = f'{a.bin_dir}/vobjcopy', f'{a.bin_dir}/vnm'
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0',
               UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    bad = 0
    with tempfile.TemporaryDirectory() as t:
        open(f'{t}/a.c', 'w').write(SRC)
        subprocess.run([vpp, '--target=aarch64-linux-gnu', '-c', f'{t}/a.c', '-o', f'{t}/a.o'], check=True, env=env)
        o = open(f'{t}/a.o', 'rb').read()
        # an odd-sized member first puts the object at an odd offset (misaligned headers)
        cases = [('good', o, True), ('good-ar', ar([('o.o/', o)]), True),
                 ('good-ar-odd', ar([('x.txt/', b'x'), ('o.o/', o)]), 'odd')]
        cases += [(n, b, False) for n, b in obj_cases(o)]
        cases += [(n, b, False) for n, b in ar_cases(o)]
        cases = [c + (False,) for c in cases]
        if has_wasm(vpp):
            open(f'{t}/w.c', 'w').write(SRC)
            subprocess.run([vpp, '--target=wasm64-unknown-emscripten', '-c', f'{t}/w.c', '-o', f'{t}/w.o'], check=True, env=env)
            cases += [(n, b, k) + (True,) for n, b, k in wasm_cases(open(f'{t}/w.o', 'rb').read())]
        else:
            print('wasm cases left out: vpp is built without WebAssembly (make VCC_WASM=1)')
        for name, data, ok, wasm in cases:
            ext = '.a' if name.startswith(('ar-', 'good-ar')) or name.endswith('-ar') or name.startswith('wasm-ar') else '.o'
            f = f'{t}/{name}{ext}'
            open(f, 'wb').write(data)
            if wasm:
                cmd = [vlink, '-mwasm64', '--no-entry'] + (['--export=_start'] if ext == '.a' else []) + [f, '-o', f'{t}/out']
            else:
                cmd = [vlink] + (['--whole-archive'] if ext == '.a' else []) + [f, '-o', f'{t}/out']
            p = subprocess.run(cmd, capture_output=True, text=True, env=env, timeout=60)
            err = p.stderr
            san = 'Sanitizer' in err or 'runtime error' in err
            if p.returncode < 0 or san or p.returncode > 1:
                why = f'crash rc={p.returncode}'
            elif ok is True and p.returncode:
                why = 'good input rejected'
            elif ok is False and (p.returncode != 1 or 'error:' not in err):
                why = 'accepted'
            else:
                why = None
            if why:
                bad += 1
                print(f'FAIL {name}: {why}\n  ' + '\n  '.join(err.splitlines()[:6]))
            elif os.environ.get('V'):
                print(f'ok   {name}: {err.splitlines()[0] if err else "linked"}')
            if ext == '.o' and not wasm:	# vobjcopy, vnm: no crash; a good object works
                for tool in ([vobjcopy, '-O', 'binary', f, f'{t}/bin'], [vnm, f]):
                    q = subprocess.run(tool, capture_output=True, text=True, env=env)
                    e2 = q.stderr
                    if q.returncode < 0 or q.returncode > 1 or 'Sanitizer' in e2 or 'runtime error' in e2 or \
                       (ok is True and q.returncode):
                        bad += 1
                        print(f'FAIL {name}: {os.path.basename(tool[0])} rc={q.returncode}\n  ' +
                              '\n  '.join(e2.splitlines()[:6]))
        print(f'vlink-bad: {len(cases) - bad}/{len(cases)} ok')
    sys.exit(1 if bad else 0)

if __name__ == '__main__': main()
