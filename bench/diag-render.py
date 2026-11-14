#!/usr/bin/env python3
"""
vcc's text diagnostics rebuilt from its JSON records (PLAN-diag.md test
layer 5a, GCC's sarif-replay idea): the records must hold everything the
text shows.  Reads `-fdiagnostics-format=json` lines and the source files
they name (physical "path", relative to --cwd), never "rendered", and
prints what vcc's text output prints, byte for byte, including the
"In file included from" lines and the "N errors generated." summary.
Two styles, as vcc's: "vcc" (the default, src/diag/render-vcc.c: several
lines, labels, folded notes, help) and "clang" (-fdiagnostics-style=clang).
A change to either renderer in C changes this file too.

  bench/diag-render.py [--cwd DIR] [--no-caret] [--style clang] [--parseable-fixits] < records.jsonl
  import: render(lines, cwd, caret=True, style='vcc', parseable=False) -> bytes
"""
import re, json, os, sys


def cp_width(c):
    """diag/source-line.c diag_codepoint_width: 2 for East Asian wide and emoji, 0 for combining marks"""
    if 0x300 <= c <= 0x36f or 0x200b <= c <= 0x200f or 0xfe00 <= c <= 0xfe0f:
        return 0
    if (0x1100 <= c <= 0x115f or (0x2e80 <= c <= 0xa4cf and c != 0x303f) or 0xac00 <= c <= 0xd7a3 or
            0xf900 <= c <= 0xfaff or 0xfe30 <= c <= 0xfe4f or 0xff00 <= c <= 0xff60 or 0xffe0 <= c <= 0xffe6 or
            0x1f300 <= c <= 0x1f64f or 0x1f900 <= c <= 0x1f9ff or 0x20000 <= c <= 0x3fffd):
        return 2
    return 1


def pad_at(s, ls, le, f):
    """diag/render-clang.c diag_snippet_is_padded_at: an insertion widens clang's caret line unless a blank is next to it"""
    x = f['start']['offset']
    if f.get('end') and f['end'].get('offset') != x:
        return False
    return (x == ls or s[x - 1] not in (32, 9)) and (x >= le or s[x] not in (32, 9))


def dcols(s, b, e, col=0):
    """display columns of s[b:e] from col (diag/source-line.c diag_columns_width): tabs to the next multiple of 8,
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
    def __init__(self, cwd, caret=True, style='vcc', parseable=False):
        self.cwd, self.caret, self.last, self.style, self.parseable = cwd, caret, None, style, parseable
        self.src = {}
        self.dlast, self.dlastlv = None, None

    def source(self, path):
        if path not in self.src:
            p = path if os.path.isabs(path) else os.path.join(self.cwd, path)
            try:
                self.src[path] = open(p, 'rb').read()
            except OSError:
                self.src[path] = None
        return self.src[path]

    def snippet(self, d, out):
        """diag.c snippet: the caret line, grown to the lines of the (unlabelled) ranges, at
        most 16 (clang's maybeAddRange); the fix-it line after the caret line"""
        loc = d['location']
        s = self.source(loc['path'])
        off = loc['offset']
        if s is None or off >= len(s):
            return

        def bol(o):
            while o > 0 and s[o - 1] != 10:
                o -= 1
            return o

        def eol(o):
            while o < len(s) and s[o] != 10:
                o += 1
            return o

        def same(x):
            return x and x.get('file') is not None and x['path'] == loc['path']
        ls, le = bol(off), eol(off)
        if le > ls and s[le - 1] == 13:
            le -= 1
        line = loc['line']
        width = max(4, len(str(line + 16)))
        l0 = l1 = line
        for sp in d['spans']:
            if not same(sp['start']) or not same(sp['end']) or sp['label']:
                continue
            rb, re_ = sp['start']['offset'], sp['end']['offset']
            if re_ < rb or re_ > len(s):
                continue
            a = line + s.count(b'\n', off, rb) if rb >= off else line - s.count(b'\n', rb, off)
            ze = re_ - 1 if re_ > rb and s[re_ - 1] == 10 else re_
            z = line + s.count(b'\n', off, ze) if ze >= off else line - s.count(b'\n', ze, off)
            mn, mx = min(a, l0), max(z, l1)
            if mx - mn + 1 <= 16:
                l0, l1 = mn, mx
        st = ls
        for _ in range(line - l0):
            if st == 0:
                break
            st = bol(st - 1)
        for k in range(l0, l1 + 1):
            en = eol(st)
            ee = en - 1 if en > st and s[en - 1] == 13 else en
            self.sn_line(s, d, out, st, ee, k, width, k == line, off, same)
            if k == line:
                self.sn_fixits(s, d, out, ls, le, width, same)
            if en >= len(s):
                break
            st = en + 1

    def sn_line(self, s, d, out, ls, le, line, width, caret, off, same):
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
        lw = dcols(s, ls, le) + 1
        m = bytearray(b' ' * lw)
        for sp in d['spans']:
            if not same(sp['start']) or sp['label']:	# (a label: vcc's style only, as diag.c)
                continue
            rb = sp['start']['offset']
            re_ = sp['end']['offset'] if same(sp['end']) else 1 << 40
            if re_ < rb or re_ <= ls or rb >= le or (re_ == ls and rb < ls):
                continue
            rb, re_ = max(rb, ls), min(re_, le)
            while rb < re_ and s[rb] in (32, 9):	# clang: first to last non-blank column
                rb += 1
            while re_ > rb and s[re_ - 1] in (32, 9):
                re_ -= 1
            for c0 in range(dcols(s, ls, rb), min(dcols(s, ls, re_), lw)):
                m[c0] = ord('~')
        for f in d['fixits']:	# a fix-it's replaced range, without its blanks at either end
            if not same(f['start']) or not same(f.get('end')):
                continue
            rb, re_ = f['start']['offset'], f['end']['offset']
            if re_ <= rb or rb < ls or rb >= le:
                continue
            re_ = min(re_, le)
            while rb < re_ and s[rb] in (32, 9):
                rb += 1
            while re_ > rb and s[re_ - 1] in (32, 9):
                re_ -= 1
            for c0 in range(dcols(s, ls, rb), min(dcols(s, ls, re_), lw)):
                if m[c0] == 32:
                    m[c0] = ord('~')
        if caret:
            ccol = dcols(s, ls, off)
            if ccol < lw:
                m[ccol] = ord('^')
        cl = bytes(m).rstrip(b' ')
        if caret:
            for f in d['fixits']:
                if not (same(f['start']) and ls <= f['start']['offset'] <= le and f['text']):
                    continue
                c0 = dcols(s, ls, f['start']['offset'])
                if pad_at(s, ls, le, f) and c0 > len(cl):
                    cl += b' ' * (c0 - len(cl))
        if cl:
            out += b' ' * (width + 2) + b'| ' + cl + b'\n'

    def sn_fixits(self, s, d, out, ls, le, width, same):
        fixes = [f for f in d['fixits'] if same(f['start']) and ls <= f['start']['offset'] <= le and f['text']]
        fl = bytearray()
        for f in fixes:
            c0 = max(dcols(s, ls, f['start']['offset']), len(fl))
            fl += b' ' * (c0 - len(fl)) + f['text'].encode()
        if fixes:
            out += b' ' * (width + 2) + b'| ' + bytes(fl) + b'\n'

    # ---- vcc's style (src/diag/render-vcc.c) ----

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
        """render-vcc.c mk: a range's mark in this file, or None"""
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
                k.get('id') == 'note_macro_expansion_here' or
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
                 f['start']['path'] == path and ls <= f['start']['offset'] <= le and f['text']]
        for f in fixes:
            c0 = dcols(s, ls, f['start']['offset'])
            if rn < c0 <= lw and pad_at(s, ls, le, f):
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
        for f in d['fixits']:	# a fix-it's replaced range, blanks trimmed
            st, en = f['start'], f.get('end')
            if len(marks) >= 32 or not st or not en or st.get('file') is None or en.get('file') is None or \
               st['path'] != path or en['path'] != path:
                continue
            rb, re_ = st['offset'], en['offset']
            if re_ <= rb or re_ > len(s):
                continue
            while rb < re_ and s[rb] in (32, 9):
                rb += 1
            while re_ > rb and s[re_ - 1] in (32, 9):
                re_ -= 1
            if re_ > rb:
                m = self.mk(s, path, {'file': path, 'path': path, 'offset': rb}, {'file': path, 'path': path, 'offset': re_}, '~', None)
                if m:
                    m['pri'] = 1
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
        for a in (d['art'].split('\n') if d.get('art') else []):
            out += b' ' * (width + 2) + a.encode() + b'\n'
        if d.get('help'):
            out += b' ' * (width + 2) + b'= help: ' + d['help'].encode() + b'\n'
        # whole lines a fix-it inserts away from the shown ones (an #include), as a patch line
        for f in d['fixits']:
            st, en, t = f['start'], f['end'], f['text']
            if (st is None or st.get('file') is None or st['path'] != path or en is None or
                    en['offset'] != st['offset'] or st['offset'] > len(s) or not t.endswith('\n') or
                    '\n' in t[:-1] or self.lstart(s, st['offset']) != st['offset'] or st['offset'] in ln):
                continue
            num = str(self.line_no(s, d, st['offset']))
            out += b' ' * (width - len(num)) + num.encode() + b' + ' + t[:-1].encode() + b'\n'

    @staticmethod
    def esc(s):
        """LLVM's write_escaped (diag.c esc)"""
        o = ''
        for ch in s.encode():
            c = chr(ch)
            if c in '\\"':
                o += '\\' + c
            elif c == '\t':
                o += '\\t'
            elif c == '\n':
                o += '\\n'
            elif ch < 0x20 or ch >= 0x7f:
                o += '\\%03o' % ch
            else:
                o += c
        return o

    def fixits(self, d, out):
        """-fdiagnostics-parseable-fixits (diag.c parseable_fixits)"""
        if not self.parseable:
            return
        for f in d['fixits']:
            b, e = f['start'], f['end']
            if b is None or e is None or b['file'] != e['file']:
                continue
            out += (f'fix-it:"{self.esc(b["file"])}":{{{b["line"]}:{b["column"]}-{e["line"]}:{e["column"]}}}:'
                    f'"{self.esc(f["text"])}"\n').encode()

    def vcc(self, d, out):
        s = self.head(d, out)
        fold = []
        if s is not None:
            path = d['location']['path']
            pls = self.lstart(s, d['location']['offset'])
            fold = [k for k in d['children'] if self.foldable(s, path, k, pls)][:32]
            self.snippet_vcc(s, d, fold, out)
            self.fixits(d, out)
            for k in fold:
                self.fixits(k, out)
        elif d.get('help'):
            out += b'      = help: ' + d['help'].encode() + b'\n'
        kids = d['children']
        mac = lambda q: q.get('id') == 'note_macro_expansion_here'
        skip = lambda q: q['level'] == 'note' and q['location'] is None and q['message'].startswith('(skipping ')
        first = last = None
        for i, k in enumerate(kids):
            folded = any(k is f for f in fold)
            if last is None and not folded and mac(k):
                # render-vcc.c diag_note_macro_run_last: 3+ frames show the first and last snippets only
                j, nm, lj = i, 0, i
                while j < len(kids) and (mac(kids[j]) or skip(kids[j])):
                    if mac(kids[j]):
                        nm, lj = nm + 1, j
                    j += 1
                if nm >= 3:
                    first, last = i, lj
            ks = self.head(k, out)
            if ks is not None and not folded and (last is None or i in (first, last)):
                self.snippet_vcc(ks, k, [], out)
                self.fixits(k, out)
            elif not folded and k.get('help'):
                out += b'      = help: ' + k['help'].encode() + b'\n'
            if i == last:
                first = last = None

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
        # clang: no snippet again at the previous record's location (no ranges, no fix-its),
        # unless a non-note follows a note
        key = (loc['path'], loc['offset']) if has else None
        same = (key is not None and key == self.dlast and not d['spans'] and not d['fixits'] and
                not (self.dlastlv == 'note' and not note))
        self.dlast, self.dlastlv = key, d['level']
        if has and self.caret and not same:
            self.snippet(d, out)
            self.fixits(d, out)


def render(lines, cwd='.', caret=True, style='vcc', parseable=False):
    r = Renderer(cwd, caret, style, parseable)
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
    sys.stdout.buffer.write(render(sys.stdin.read().splitlines(), cwd, '--no-caret' not in a, style,
                                   '--parseable-fixits' in a))
    return 0


if __name__ == '__main__':
    sys.exit(main())
