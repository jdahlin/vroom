#!/usr/bin/env python3
"""
vcc's text diagnostics rebuilt from its JSON records (PLAN-diag.md test
layer 5a, GCC's sarif-replay idea): the records must hold everything the
text shows.  Reads `-fdiagnostics-format=json` lines and the source files
they name (physical "path", relative to --cwd), never "rendered", and
prints what vcc's text output prints, byte for byte, including the
"In file included from" lines and the "N errors generated." summary.
Two styles, as vcc's: "vcc" (the default, ccs/vcc/pp/diagtext.c: several
lines, labels, folded notes, help) and "clang" (-fdiagnostics-style=clang).
A change to either renderer in C changes this file too.

  bench/diag-render.py [--cwd DIR] [--no-caret] [--style clang] < records.jsonl
  import: render(lines, cwd, caret=True, style='vcc') -> bytes
"""
import json, os, sys


def cp_width(c):
    """pp/diag.c cp_width: 2 for East Asian wide and emoji, 0 for combining marks"""
    if 0x300 <= c <= 0x36f or 0x200b <= c <= 0x200f or 0xfe00 <= c <= 0xfe0f:
        return 0
    if (0x1100 <= c <= 0x115f or (0x2e80 <= c <= 0xa4cf and c != 0x303f) or 0xac00 <= c <= 0xd7a3 or
            0xf900 <= c <= 0xfaff or 0xfe30 <= c <= 0xfe4f or 0xff00 <= c <= 0xff60 or 0xffe0 <= c <= 0xffe6 or
            0x1f300 <= c <= 0x1f64f or 0x1f900 <= c <= 0x1f9ff or 0x20000 <= c <= 0x3fffd):
        return 2
    return 1


def dcols(s, b, e, col=0):
    """display columns of s[b:e] from col (pp/diag.c dcols): tabs to the next multiple of 8,
    UTF-8 by its code point's width, a malformed byte one column"""
    i = b
    while i < e:
        ch = s[i]
        if ch == 9:
            col = (col // 8 + 1) * 8
        elif ch < 0xc0:
            col += (ch & 0xc0) != 0x80
        else:
            n = 3 if ch >= 0xf0 else 2 if ch >= 0xe0 else 1
            c = ch & (0x3f >> n)
            k = 0
            while k < n and i + 1 + k < e and s[i + 1 + k] & 0xc0 == 0x80:
                c = c << 6 | (s[i + 1 + k] & 0x3f)
                k += 1
            col += cp_width(c) if k == n else 1
            i += k
        i += 1
    return col


class Renderer:
    def __init__(self, cwd, caret=True, style='vcc'):
        self.cwd, self.caret, self.last, self.style = cwd, caret, None, style
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

    # ---- vcc's style (ccs/vcc/pp/diagtext.c) ----

    def head(self, d, out):
        """the first line (and include stack); the source and the location's offset, or None"""
        loc = d['location']
        has = loc is not None and loc.get('file') is not None
        if has and d['level'] != 'note':
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
        if not has or not self.caret:
            return None
        s = self.source(loc['path'])
        if s is None or loc['offset'] >= len(s):
            return None
        return s

    @staticmethod
    def lstart(s, off):
        while off > 0 and s[off - 1] != 10:
            off -= 1
        return off

    @staticmethod
    def lend(s, ls):
        le = ls
        while le < len(s) and s[le] != 10:
            le += 1
        if le > ls and s[le - 1] == 13:
            le -= 1
        return le

    @staticmethod
    def lnext(s, ls):
        while ls < len(s) and s[ls] != 10:
            ls += 1
        return ls + 1 if ls < len(s) else len(s)

    @staticmethod
    def lcount(s, a, b):
        return s.count(b'\n', a, b)

    def mk(self, s, path, start, end, ch, label):
        """diagtext.c mk: a range's mark in this file, or None"""
        if start is None or start.get('file') is None or start['path'] != path:
            return None
        b = start['offset']
        en = end['offset'] if end and end.get('file') is not None and end['path'] == path else len(s)
        size = len(s)
        if en < b or b > size:
            return None
        en = min(en, size)
        if en == b:
            en = b + 1
        last = en - 1 if en - 1 < size else (size - 1 if size else 0)
        lb, le = self.lstart(s, b), self.lstart(s, last)
        return {'b': b, 'e': en, 'lb': lb, 'le': max(le, lb), 'label': label, 'ch': ch,
                'pri': 3 if ch == '^' else 2 if ch == '~' else 1}

    def foldable(self, s, path, k, pls):
        loc = k['location']
        if (k['level'] != 'note' or k['fixits'] or k.get('help') or k.get('children') or loc is None or
                loc.get('file') is None or loc['path'] != path or loc['offset'] >= len(s)):
            return False
        ls = self.lstart(s, loc['offset'])
        for sp in k['spans']:
            st, en = sp['start'], sp['end']
            if (st is None or st.get('file') is None or st['path'] != path or en is None or en.get('file') is None or
                    en['path'] != path or en['offset'] < st['offset'] or en['offset'] > len(s) or
                    self.lstart(s, st['offset']) != ls or sp['label']):
                return False
        return (self.lcount(s, ls, pls) if ls <= pls else self.lcount(s, pls, ls)) <= 5

    def line_no(self, s, d, ls):
        """the line number of the line at ls: the location's, moved by the newlines between"""
        off = d['location']['offset']
        return d['location']['line'] + (self.lcount(s, off, ls) if ls >= off else -self.lcount(s, ls, off))

    def one_line(self, s, d, marks, ls, width, out):
        path = d['location']['path']
        le = self.lend(s, ls)
        lw = dcols(s, ls, le) + 1
        num = str(self.line_no(s, d, ls)).encode()
        out += b' ' * (width - len(num) + 1) + num + b' | '
        col = 0
        for i in range(ls, le):
            if s[i] == 9:
                to = (col // 8 + 1) * 8
                out += b' ' * (to - col)
                col = to
            else:
                out.append(s[i])
                col = dcols(s, i, i + 1, col)
        out += b'\n'
        row = bytearray(b' ' * (lw + 1))
        pri = [0] * (lw + 2)
        labs = []	# (anchor, mark), rightmost first; equal anchors in mark order
        maxc0 = 0
        for m in marks:
            if m['le'] < ls or m['lb'] > ls:
                continue
            rb, re_ = max(m['b'], ls), min(m['e'], le)
            c0 = dcols(s, ls, rb)
            c1 = dcols(s, ls, re_) if re_ > rb else c0 + 1
            if c1 <= c0:
                c1 = c0 + 1
            c = c0
            while c < c1 and c <= lw:
                if m['pri'] > pri[c]:
                    row[c] = ord(m['ch'])
                    pri[c] = m['pri']
                c += 1
            maxc0 = max(maxc0, c0)
            if m['label'] and m['le'] == ls and len(labs) < 32:
                j = len(labs)
                while j > 0 and labs[j - 1][0] < c0:
                    j -= 1
                labs.insert(j, (c0, m))
        rn = len(bytes(row[:lw + 1]).rstrip(b' '))
        fixes = [f for f in d['fixits'] if f['start'] and f['start'].get('file') is not None and
                 f['start']['path'] == path and ls <= f['start']['offset'] <= le]
        for f in fixes:
            c0 = dcols(s, ls, f['start']['offset'])
            if rn < c0 <= lw:
                rn = c0
        if not rn and not fixes:
            return
        bar = b' ' * (width + 2) + b'| '
        out += bar + bytes(row[:rn])
        inl = bool(labs) and rn > 0 and labs[0][0] >= maxc0
        if inl:
            out += b' ' + labs[0][1]['label'].encode()
        out += b'\n'
        for k in range(int(inl), len(labs)):
            for pas in (0, 1):
                r = bytearray()
                col = 0
                for j in range(len(labs) - 1, k - 1, -1):
                    c = labs[j][0]
                    if col > c or (j > k and c == labs[k][0]):
                        continue
                    r += b' ' * (c - col)
                    if pas == 1 and j == k:
                        r += labs[j][1]['label'].encode()
                        break
                    r += b'|'
                    col = c + 1
                out += bar + bytes(r) + b'\n'
        if fixes:
            fl = bytearray()
            for f in fixes:
                c0 = max(dcols(s, ls, f['start']['offset']), len(fl))
                fl += b' ' * (c0 - len(fl)) + f['text'].encode()
            out += bar + bytes(fl) + b'\n'

    def snippet_vcc(self, s, d, fold, out):
        path, off = d['location']['path'], d['location']['offset']
        pls = self.lstart(s, off)
        marks = [self.mk(s, path, d['location'], {'file': path, 'path': path, 'offset': off + 1}, '^', d['label'])]
        for sp in d['spans']:
            if len(marks) < 32:
                m = self.mk(s, path, sp['start'], sp['end'], '~', sp['label'])
                if m:
                    marks.append(m)
        for k in fold:
            kl = k['location']
            if len(marks) < 32:
                m = self.mk(s, path, kl, {'file': path, 'path': path, 'offset': kl['offset'] + 1}, '-', k['message'])
                if m:
                    marks.append(m)
            for sp in k['spans']:
                if len(marks) < 32:
                    m = self.mk(s, path, sp['start'], sp['end'], '-', None)
                    if m:
                        marks.append(m)
        ln = []

        def add(x):
            if x not in ln and len(ln) < 12:
                ln.append(x)
        add(pls)
        for m in marks:
            nl, x = self.lcount(s, m['lb'], m['le']), m['lb']

            def dist(a):
                return self.lcount(s, min(a, pls), max(a, pls))
            if dist(m['lb']) > 20 and dist(m['le']) > 20:
                continue
            k = 0
            while k <= nl and k < 4:
                if nl < 4 or k < 2:
                    add(x)
                k += 1
                x = self.lnext(s, x)
            add(m['le'])
        ln.sort()
        maxl = max(self.line_no(s, d, x) for x in ln)
        width = max(4, len(str(maxl + 16)))
        for i, x in enumerate(ln):
            if i:
                gap = self.lcount(s, ln[i - 1], x)
                if gap == 2:
                    self.one_line(s, d, marks, self.lnext(s, ln[i - 1]), width, out)
                elif gap > 2:
                    out += b' ' * (width - 2) + b'...\n'
            self.one_line(s, d, marks, x, width, out)
        if d.get('help'):
            out += b' ' * (width + 2) + b'= help: ' + d['help'].encode() + b'\n'

    def vcc(self, d, out):
        s = self.head(d, out)
        fold = []
        if s is not None:
            path = d['location']['path']
            pls = self.lstart(s, d['location']['offset'])
            fold = [k for k in d['children'] if self.foldable(s, path, k, pls)][:32]
            self.snippet_vcc(s, d, fold, out)
        elif d.get('help'):
            out += b'      = help: ' + d['help'].encode() + b'\n'
        for k in d['children']:
            folded = any(k is f for f in fold)
            ks = self.head(k, out)
            if ks is not None and not folded:
                self.snippet_vcc(ks, k, [], out)
            elif not folded and k.get('help'):
                out += b'      = help: ' + k['help'].encode() + b'\n'

    # ---- clang's style ----

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


def render(lines, cwd='.', caret=True, style='vcc'):
    r = Renderer(cwd, caret, style)
    out = bytearray()
    errors = warns = 0
    for l in lines:
        if not l.strip():
            continue
        d = json.loads(l)
        if style == 'vcc':
            r.vcc(d, out)
        else:
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
    style = a[a.index('--style') + 1] if '--style' in a else 'vcc'
    sys.stdout.buffer.write(render(sys.stdin.read().splitlines(), cwd, '--no-caret' not in a, style))
    return 0


if __name__ == '__main__':
    sys.exit(main())
