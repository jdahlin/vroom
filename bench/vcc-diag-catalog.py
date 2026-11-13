#!/usr/bin/env python3
"""
The diagnostics catalog (PLAN-diag.md test layer 10): one HTML page with
every src/test/diag case, its source, clang's text, vcc's text in the
vcc style (in color, as a terminal shows it), the narrated style and the
JSON records, side by side.  For review by eye: a change to the renderer or
to a message shows up as a page to read, not only as snapshot diffs.

  bench/vcc-diag-catalog.py [-o FILE] [--bin-dir DIR] [PATTERN]
      default FILE: build/src/diag-catalog.html
"""
import html, importlib.util, json, os, re, sys
from multiprocessing import Pool

R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
spec = importlib.util.spec_from_file_location('vccdiag', f'{R}/bench/vcc-diag.py')
vd = importlib.util.module_from_spec(spec)
spec.loader.exec_module(vd)

SGR = re.compile(r'\x1b\[([0-9;]*)m|\x1b\]8;;([^\x1b]*)\x1b\\')
COLORS = {'30': 'k', '31': 'r', '32': 'g', '33': 'y', '34': 'b', '35': 'm', '36': 'c', '37': 'w'}


def ansi_html(s):
    """SGR colors and OSC 8 links as spans and anchors"""
    out, open_span, open_a, pos = [], False, False, 0
    for m in SGR.finditer(s):
        out.append(html.escape(s[pos:m.start()]))
        pos = m.end()
        if m.group(2) is not None:	# OSC 8: a link starts (url) or ends (empty)
            if open_a:
                out.append('</a>'); open_a = False
            if m.group(2):
                out.append(f'<a href="{html.escape(m.group(2))}">'); open_a = True
            continue
        if open_span:
            out.append('</span>'); open_span = False
        cls = []
        for c in (m.group(1) or '0').split(';'):
            if c == '1':
                cls.append('bold')
            elif c in COLORS:
                cls.append(COLORS[c])
        if cls:
            out.append(f'<span class="{" ".join(cls)}">'); open_span = True
    out.append(html.escape(s[pos:]))
    if open_span:
        out.append('</span>')
    if open_a:
        out.append('</a>')
    return ''.join(out)


def one(name):
    src = open(os.path.join(vd.D, name), errors='replace').read()
    _, vcc = vd.run([vd.VPP], name, ['-fcolor-diagnostics'])
    _, narr = vd.run([vd.VPP], name, ['-fdiagnostics-style=narrated'])
    _, js = vd.run([vd.VPP], name, ['-fdiagnostics-format=json'])
    try:
        _, cl = vd.run([vd.CL], name)
    except OSError:
        cl = '(no clang)'
    recs = []
    for l in js.splitlines():
        try:
            r = json.loads(l)
            r.pop('rendered', None)
            recs.append(r)
        except ValueError:
            pass
    return name, src, vcc, narr, cl, json.dumps(recs, indent=1)


CSS = '''
:root { --bg: #fdfdfb; --fg: #1d1d1b; --mut: #6b6b66; --line: #e3e2dc; --pane: #f4f3ee;
  --r: #c0262d; --g: #1f7a3a; --y: #8a6100; --b: #1f5fbf; --m: #8e2a9c; --c: #11707a; --k: #55554f; }
@media (prefers-color-scheme: dark) { :root { --bg: #161615; --fg: #e8e7e2; --mut: #9a9993; --line: #2e2d2a;
  --pane: #1f1f1d; --r: #ff6b6b; --g: #6bd68b; --y: #e5b84a; --b: #79a8ff; --m: #d58cf0; --c: #5fd0d8; --k: #8f8e88; } }
body { background: var(--bg); color: var(--fg); font: 14px/1.45 system-ui, sans-serif; margin: 0 16px 40px; }
h1 { font-size: 20px; margin: 20px 0 4px; } p.sub { color: var(--mut); margin: 0 0 16px; }
input { font: inherit; padding: 6px 10px; width: min(420px, 100%); border: 1px solid var(--line);
  border-radius: 6px; background: var(--pane); color: var(--fg); }
section { border-top: 1px solid var(--line); padding: 14px 0; }
h2 { font: 600 15px ui-monospace, monospace; margin: 0 0 8px; }
.grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(420px, 1fr)); gap: 10px; }
.p { background: var(--pane); border-radius: 6px; padding: 8px 10px; overflow-x: auto; min-width: 0; }
.p b { display: block; font: 600 11px system-ui; text-transform: uppercase; letter-spacing: .05em;
  color: var(--mut); margin-bottom: 4px; }
pre { margin: 0; font: 12.5px/1.4 ui-monospace, Menlo, monospace; white-space: pre; }
details pre { max-height: 360px; overflow: auto; }
.bold { font-weight: 700; } .r { color: var(--r); } .g { color: var(--g); } .y { color: var(--y); }
.b { color: var(--b); } .m { color: var(--m); } .c { color: var(--c); } .k { color: var(--k); } .w { color: var(--fg); }
a { color: inherit; }
'''

JS = '''
const q = document.getElementById('q');
q.addEventListener('input', () => {
  const s = q.value.toLowerCase();
  for (const e of document.querySelectorAll('section'))
    e.hidden = s && !e.textContent.toLowerCase().includes(s);
});
'''


def main():
    a = sys.argv[1:]
    out = f'{vd.BUILD}/diag-catalog.html'
    if '-o' in a:
        k = a.index('-o'); out = a[k + 1]; del a[k:k + 2]
    pat = a[0] if a else ''
    names = sorted(n for n in os.listdir(vd.D) if n.endswith('.c') and pat in n)
    with Pool(6) as pool:
        res = pool.map(one, names)
    h = ['<!doctype html><html lang="en"><head><meta charset="utf-8">',
         '<meta name="viewport" content="width=device-width, initial-scale=1">',
         f'<title>vcc diagnostics catalog</title><style>{CSS}</style></head><body>',
         '<h1>vcc diagnostics catalog</h1>',
         f'<p class="sub">{len(res)} cases from src/test/diag: clang, vcc (default style), narrated, JSON records</p>',
         '<input id="q" placeholder="filter: name, message, flag" autofocus>']
    for name, src, vcc, narr, cl, js in res:
        h.append(f'<section id="{html.escape(name)}"><h2><a href="#{html.escape(name)}">{html.escape(name)}</a></h2>')
        h.append('<div class="grid">')
        h.append(f'<div class="p"><b>source</b><pre>{html.escape(src)}</pre></div>')
        h.append(f'<div class="p"><b>vcc</b><pre>{ansi_html(vcc)}</pre></div>')
        h.append(f'<div class="p"><b>clang</b><pre>{html.escape(cl)}</pre></div>')
        h.append(f'<div class="p"><b>narrated</b><pre>{html.escape(narr)}</pre></div>')
        h.append('</div>')
        h.append(f'<details><summary>JSON records</summary><div class="p"><pre>{html.escape(js)}</pre></div></details>')
        h.append('</section>')
    h.append(f'<script>{JS}</script></body></html>\n')
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    open(out, 'w').write('\n'.join(h))
    print(out)


if __name__ == '__main__':
    main()
