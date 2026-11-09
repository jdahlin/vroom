#!/usr/bin/env python3
"""
vcc's text diagnostics rebuilt from its JSON records (PLAN-diag.md test
layer 5a, GCC's sarif-replay idea): the records must hold everything the
text shows.  Reads `-fdiagnostics-format=json` lines and the source files
they name (physical "path", relative to --cwd), never "rendered", and
prints what vcc's text output prints, byte for byte, including the
"In file included from" lines and the "N errors generated." summary.

  bench/diag-render.py [--cwd DIR] [--no-caret] < records.jsonl
  import: render(lines, cwd, caret=True) -> bytes
"""
import json, os, sys


def dcols(s, b, e, col=0):
    """display columns of s[b:e]: tabs to the next multiple of 8, UTF-8 continuation bytes none"""
    for ch in s[b:e]:
        if ch == 9:
            col = (col // 8 + 1) * 8
        elif ch & 0xc0 != 0x80:
            col += 1
    return col


class Renderer:
    def __init__(self, cwd, caret=True):
        self.cwd, self.caret, self.last = cwd, caret, None
        self.src = {}

    def source(self, path):
        if path not in self.src:
            p = path if os.path.isabs(path) else os.path.join(self.cwd, path)
            try:
                self.src[path] = open(p, 'rb').read()
            except OSError:
                self.src[path] = None
        return self.src[path]

    def snippet(self, d, out):
        loc = d['location']
        s = self.source(loc['path'])
        off = loc['offset']
        if s is None or off >= len(s):
            return
        ls = off
        while ls > 0 and s[ls - 1] != 10:
            ls -= 1
        le = off
        while le < len(s) and s[le] != 10:
            le += 1
        if le > ls and s[le - 1] == 13:
            le -= 1
        line = loc['line']
        width = max(4, len(str(line + 16)))
        num = str(line).encode()
        text = bytearray()
        col = 0
        for ch in s[ls:le]:
            if ch == 9:
                to = (col // 8 + 1) * 8
                text += b' ' * (to - col)
                col = to
            else:
                text.append(ch)
                if ch & 0xc0 != 0x80:
                    col += 1
        out += b' ' * (width - len(num) + 1) + num + b' | ' + bytes(text) + b'\n'

        def same(x):
            return x and x.get('file') is not None and x['path'] == loc['path']
        lw = dcols(s, ls, le) + 1
        m = bytearray(b' ' * lw)
        for sp in d['spans']:
            if not same(sp['start']):
                continue
            rb = sp['start']['offset']
            re_ = sp['end']['offset'] if same(sp['end']) else 1 << 40
            if re_ < rb or re_ <= ls or rb >= le:
                continue
            rb, re_ = max(rb, ls), min(re_, le)
            for c0 in range(dcols(s, ls, rb), min(dcols(s, ls, re_), lw)):
                m[c0] = ord('~')
        ccol = dcols(s, ls, off)
        if ccol < lw:
            m[ccol] = ord('^')
        cl = bytes(m).rstrip(b' ')
        fixes = [f for f in d['fixits'] if same(f['start']) and ls <= f['start']['offset'] <= le]
        for f in fixes:
            c0 = dcols(s, ls, f['start']['offset'])
            if c0 > len(cl):
                cl += b' ' * (c0 - len(cl))
        out += b' ' * (width + 2) + b'| ' + cl + b'\n'
        fl = bytearray()
        for f in fixes:
            c0 = max(dcols(s, ls, f['start']['offset']), len(fl))
            fl += b' ' * (c0 - len(fl)) + f['text'].encode()
        if fixes:
            out += b' ' * (width + 2) + b'| ' + bytes(fl) + b'\n'

    def one(self, d, out):
        loc = d['location']
        has = loc is not None and loc.get('file') is not None
        note = d['level'] == 'note'
        if has and not note:
            key = (loc['path'], json.dumps(d['include_stack']))
            if key != self.last:
                for i in d['include_stack']:
                    out += f'In file included from {i["file"]}:{i["line"]}:\n'.encode()
            self.last = key
        if has:
            out += f'{loc["file"]}:{loc["line"]}:{loc["column"]}: '.encode()
        elif loc is not None:
            out += b'vcc: '
        out += f'{d["level"]}: '.encode() + d['message'].encode()
        if d['flag']:
            out += f' [{"-Werror," if d["werror"] else ""}-W{d["flag"]}]'.encode()
        out += b'\n'
        if has and self.caret:
            self.snippet(d, out)


def render(lines, cwd='.', caret=True):
    r = Renderer(cwd, caret)
    out = bytearray()
    errors = warns = 0
    for l in lines:
        if not l.strip():
            continue
        d = json.loads(l)
        r.one(d, out)
        for k in d['children']:
            r.one(k, out)
        if d['level'] in ('error', 'fatal error'):
            errors += 1
        elif d['level'] == 'warning':
            warns += 1
    def pl(n, w):
        return f'{n} {w}{"s" if n > 1 else ""}'
    if warns and errors:
        out += f'{pl(warns, "warning")} and {pl(errors, "error")} generated.\n'.encode()
    elif errors:
        out += f'{pl(errors, "error")} generated.\n'.encode()
    elif warns:
        out += f'{pl(warns, "warning")} generated.\n'.encode()
    return bytes(out)


def main():
    a = sys.argv[1:]
    cwd = a[a.index('--cwd') + 1] if '--cwd' in a else '.'
    sys.stdout.buffer.write(render(sys.stdin.read().splitlines(), cwd, '--no-caret' not in a))
    return 0


if __name__ == '__main__':
    sys.exit(main())
