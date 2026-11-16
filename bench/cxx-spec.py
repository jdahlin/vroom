#!/usr/bin/env python3
"""Split the C++ standard draft (github.com/cplusplus/draft, LaTeX) into one
Markdown file per subclause, linked by stable name.

    bench/cxx-spec.py [--tag n4861] [--src CHECKOUT] [--out docs/cxx-spec]

Every subclause [label] becomes OUT/label.md (library clauses: OUT/lib/label.md)
with its own paragraphs, links to its parent, neighbours and subclauses, and a
link for every cross reference.  OUT/README.md is the table of contents,
OUT/grammar-index.md maps each grammar nonterminal to where it is defined.
Without --src the tag is cloned (shallow) into a temporary directory.
"""
import argparse
import collections
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unicodedata

VERB_ENVS = ('codeblock', 'codeblocktu', 'itemdecl', 'outputblock', 'codeblockdigitsep')
BNF_ENVS = ('bnf', 'ncbnf', 'simplebnf', 'ncsimplebnf', 'ncrebnf', 'rebnf', 'bnfbase', 'bnftab')
LIST_ENVS = ('itemize', 'enumerate', 'description')
PASS_ENVS = {'itemdescr': 0, 'indented': 0, 'center': 0, 'flushright': 0, 'minipage': 1,
             'addedblock': 0, 'indenthelper': 0, 'bnflist': 0, 'mcftcol': 0, 'TableBase': 0,
             'document': 0}
LIB_FIRST, LIB_LAST = 'lib-intro', 'threads'

NAME = re.compile(r'[a-zA-Z]+')
unknown = collections.Counter()
warnings = collections.Counter()


# ---------------------------------------------------------------- TeX reading

def read_group(s, i):
    """s[i] == '{': (content, index after the matching '}')."""
    depth, j, n = 0, i, len(s)
    while j < n:
        c = s[j]
        if c == '\\':
            j += 2
            continue
        if c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
            if depth == 0:
                return s[i + 1:j], j + 1
        j += 1
    return s[i + 1:], n


def skip_ws(s, i):
    n = len(s)
    while i < n and s[i] in ' \t\n':
        i += 1
    return i


def read_arg(s, i):
    j = skip_ws(s, i)
    if j >= len(s):
        return '', j
    if s[j] == '{':
        return read_group(s, j)
    if s[j] == '\\':
        m = NAME.match(s, j + 1)
        e = m.end() if m else j + 2
        return s[j:e], e
    return s[j], j + 1


def read_opt(s, i):
    """An optional [..] argument at i: (content or None, index after)."""
    if i < len(s) and s[i] == '[':
        depth, j = 0, i
        while j < len(s):
            c = s[j]
            if c == '\\':
                j += 2
                continue
            if c == '{':
                depth += 1
            elif c == '}':
                depth -= 1
            elif c == ']' and depth == 0:
                return s[i + 1:j], j + 1
            j += 1
    return None, i


def find_end(s, i, env):
    """i is after \\begin{env}: (inner, index after the matching \\end{env})."""
    b, e = '\\begin{' + env + '}', '\\end{' + env + '}'
    depth, j = 1, i
    while True:
        k = s.find(e, j)
        if k < 0:
            return s[i:], len(s)
        depth += s.count(b, j, k) - 1
        if depth == 0:
            return s[i:k], k + len(e)
        j = k + len(e)


def split_top(s, amp=False):
    """Split at top-level \\\\ (rows); with amp, at top-level & (cells)."""
    out, depth, env, i, start, n = [], 0, 0, 0, 0, len(s)
    while i < n:
        c = s[i]
        if c == '\\':
            if s.startswith('\\begin{', i):
                env += 1
            elif s.startswith('\\end{', i):
                env -= 1
            elif not amp and s[i + 1:i + 2] == '\\' and depth == 0 and env == 0:
                out.append(s[start:i])
                i += 2
                if s[i:i + 1] == '*':
                    i += 1
                _, i = read_opt(s, i)
                start = i
                continue
            i += 2
            continue
        if c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
        elif amp and c == '&' and depth == 0 and env == 0:
            out.append(s[start:i])
            start = i + 1
        i += 1
    out.append(s[start:])
    return out


def split_items(s):
    """The \\item pieces of a list body: [(label or None, text)]."""
    items, depth, env, i, n = [], 0, 0, 0, len(s)
    start = None
    while i < n:
        c = s[i]
        if c == '\\':
            if s.startswith('\\begin{', i):
                env += 1
            elif s.startswith('\\end{', i):
                env -= 1
            elif depth == 0 and env == 0 and re.match(r'\\item(?![a-zA-Z])', s[i:i + 6]):
                if start is not None:
                    items.append(s[start:i])
                start = i + 5
                i += 5
                continue
            i += 2
            continue
        if c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
        i += 1
    if start is not None:
        items.append(s[start:])
    res = []
    for it in items:
        lab, j = read_opt(it, skip_ws(it, 0) if it[:1] != '[' else 0)
        res.append((lab, it[j:] if lab is not None else it))
    return res


# ---------------------------------------------------------------- preprocessing

verbs = []


def strip_comments(t):
    def f(m):
        # "\\item%" + newline + "Scope" must not become \\itemScope
        glue = re.search(r'\\[a-zA-Z]+$', m.string[max(0, m.start() - 40):m.start()])
        return m.group(1) + (' ' if glue else '')
    return re.sub(r'(?<!\\)((?:\\\\)*)%[^\n]*\n[ \t]*', f, t)


user_macros = {}    # name -> (number of arguments, body): \\newcommand in the text
DEF_RE = re.compile(r'\\(?:re)?newcommand\{\\(\w+)\}(?:\[(\d)\])?\s*\{|\\def\\(\w+)\{')


def take_defs(t, keep=()):
    out, pos = [], 0
    for m in DEF_RE.finditer(t):
        if m.start() < pos:
            continue
        body, j = read_group(t, m.end() - 1)
        name = m.group(1) or m.group(3)
        if name not in keep:
            user_macros[name] = (int(m.group(2) or 0), body)
        out.append(t[pos:m.start()])
        pos = j
    out.append(t[pos:])
    return ''.join(out)


def expand_rowhdr(t):
    out, pos = [], 0
    for m in re.finditer(r'\\rowhdr\{', t):
        g, j = read_group(t, m.end() - 1)
        out.append(t[pos:m.start()] + '\\hdstyle{' + g + '}&')
        pos = j
    return ''.join(out) + t[pos:]


def preprocess(t):
    """Verbatim environments become \\VERB{n}; comments go away elsewhere."""
    pat = re.compile(r'\\begin\{(' + '|'.join(VERB_ENVS) + r')\}(.*?)\\end\{\1\}', re.S)
    out, pos = [], 0
    for m in pat.finditer(t):
        out.append(strip_comments(t[pos:m.start()]))
        env, body = m.group(1), m.group(2)
        title = None
        if env == 'codeblocktu':
            title, j = read_group(body, body.index('{'))
            body = body[j:]
        verbs.append((env, title, body))
        out.append('\\VERB{%d}' % (len(verbs) - 1))
        pos = m.end()
    out.append(strip_comments(t[pos:]))
    t = ''.join(out)

    def vb(m):
        verbs.append(('verb', None, m.group(2)))
        return '\\VERB{%d}' % (len(verbs) - 1)
    t = re.sub(r'\\verb(.)(.*?)\1', vb, t)
    t = re.sub(r'\\stage\{([^{}]*)\}', r'\\item[Stage \1:]', t)
    t = expand_rowhdr(t)
    return take_defs(t, keep=('requires', 'indexordmem', 'indexunordmem'))


# ---------------------------------------------------------------- document model

class Sec:
    def __init__(self, level, label, title, src, lib, annex=None):
        self.level, self.label, self.title, self.src, self.lib = level, label, title, src, lib
        self.annex = annex
        self.body = ''
        self.kids = []
        self.parent = None
        self.num = ''

    @property
    def path(self):
        return ('lib/' if self.lib else '') + self.label + '.md'


class Doc:
    def __init__(self):
        self.secs = []          # document order
        self.by_label = {}      # label -> (Sec, anchor or None)
        self.tables = {}        # label -> number
        self.figures = {}
        self.nonterms = collections.defaultdict(list)   # name -> [Sec]
        self.table_args = {'tabular': 1, 'longtable': 1, 'array': 1, 'table': 0}
        self.src = ''


SEC_RE = re.compile(r'\\rSec(\d)\[([^\]]*)\]\{|\\(infannex|normannex)\{([^}]*)\}\{')


def load(doc, src):
    doc.src = src
    std = open(os.path.join(src, 'std.tex')).read()
    files = [f for f in re.findall(r'^\\include\{([^}]+)\}', std, re.M) if f not in ('front', 'back')]
    for m in re.finditer(r'\\newenvironment\{(\w+)\}(?:\[(\d)\])?(\[[^\]]*\])?',
                         open(os.path.join(src, 'tables.tex')).read()):
        doc.table_args[m.group(1)] = int(m.group(2) or 0) - (1 if m.group(3) else 0)
    for k in ('TableBase', 'mcftcol'):
        doc.table_args.pop(k, None)
    take_defs(strip_comments(open(os.path.join(src, 'config.tex')).read()))
    lib = False
    stack = []
    gram = []       # (label, title, [bnf blocks]) per \\gramSec

    def add(s):
        while stack and stack[-1].level >= s.level:
            stack.pop()
        nonlocal chap, annex
        if stack:
            s.parent = stack[-1]
            s.parent.kids.append(s)
            s.num = '%s.%d' % (s.parent.num, len(s.parent.kids))
        elif s.annex:
            s.num = chr(ord('A') + annex)
            annex += 1
        else:
            chap += 1
            s.num = str(chap)
        stack.append(s)
        doc.secs.append(s)
        if s.label in doc.by_label:
            warnings['duplicate label ' + s.label] += 1
        doc.by_label[s.label] = (s, None)

    chap = annex = 0
    for f in files:
        if f == LIB_FIRST:
            lib = True
        t = preprocess(open(os.path.join(src, f + '.tex')).read())
        # Annex A's subclauses are the bnf blocks that follow each \\gramSec
        gs = list(re.finditer(r'\\gramSec\[([^\]]*)\]\{([^}]*)\}', t))
        for k, m in enumerate(gs):
            part = t[m.end():gs[k + 1].start() if k + 1 < len(gs) else len(t)]
            gram.append((m.group(1), m.group(2), f,
                         re.findall(r'\\begin\{bnf\}.*?\\end\{bnf\}', part, re.S)))
        t = re.sub(r'\\gramSec\[[^\]]*\]\{[^}]*\}', '', t)
        ms = list(SEC_RE.finditer(t))
        for k, m in enumerate(ms):
            title, j = read_group(t, m.end() - 1)
            if m.group(1) is not None:
                s = Sec(int(m.group(1)), m.group(2), title, f, lib)
            else:
                s = Sec(0, m.group(4), title, f, lib, annex=m.group(3))
            s.body = t[j:ms[k + 1].start() if k + 1 < len(ms) else len(t)]
            flushgram = '\\FlushAndPrintGrammar' in s.body
            s.body = s.body.replace('\\FlushAndPrintGrammar', '')
            add(s)
            if flushgram:
                for lab, title, gf, blocks in gram:
                    g = Sec(1, lab, title, gf, False)
                    g.body = '\n\n'.join(blocks)
                    g.summary = True
                    add(g)
        if f == LIB_LAST:
            lib = False
    # labels, tables, figures, nonterminals
    tn = fn = 0
    env_re = re.compile(r'\\begin\{(\w+)\}')
    for s in doc.secs:
        b = s.body
        for m in re.finditer(r'\\definition\{', b):
            _, j = read_group(b, m.end() - 1)
            lab, _ = read_arg(b, j)
            doc.by_label[lab] = (s, lab)
        for m in re.finditer(r'\\label\{([^}]*)\}', b):
            doc.by_label.setdefault(m.group(1), (s, m.group(1)))
        for m in env_re.finditer(b):
            env = m.group(1)
            if env == 'importgraphic':
                _, j = read_arg(b, m.end())
                lab, _ = read_arg(b, j)
                fn += 1
                doc.figures[lab] = fn
                doc.by_label['fig:' + lab] = (s, 'fig:' + lab)
            elif env in doc.table_args and doc.table_args[env] >= 2:
                cap, lab = table_caption(env, read_args(b, m.end(), doc.table_args[env])[0])
                if lab and lab not in doc.tables:
                    tn += 1
                    doc.tables[lab] = tn
                    doc.by_label['tab:' + lab] = (s, 'tab:' + lab)
        for env in BNF_ENVS:
            for m in re.finditer(r'\\begin\{' + env + r'\}(.*?)\\end\{' + env + r'\}', b, re.S):
                for nt in re.findall(r'\\(?:re)?nontermdef\{([^}]*)\}', m.group(1)):
                    if env == 'bnf' and not hasattr(s, 'summary') and s not in doc.nonterms[nt]:
                        doc.nonterms[nt].append(s)
    return doc


def read_args(s, i, n):
    _, i = read_opt(s, i)
    args = []
    for _ in range(n):
        a, i = read_arg(s, i)
        args.append(a)
    return args, i


def table_caption(env, args):
    if env == 'oldconcepttable':
        return '\\oldconcept{%s} requirements%s' % (args[0], args[1]), args[2]
    if len(args) >= 2:
        return args[0], args[1]
    return None, None


TABLE_HEADERS = {
    'libtab2': lambda a: (a[3], a[4]), 'longlibtab2': lambda a: (a[3], a[4]),
    'LibEffTab': lambda a: ('Element', a[2]), 'longLibEffTab': lambda a: ('Element', a[2]),
    'libefftab': lambda a: ('Element', 'Effect(s) if set'),
    'longlibefftab': lambda a: ('Element', 'Effect(s) if set'),
    'libefftabmean': lambda a: ('Element', 'Meaning'),
    'libefftabvalue': lambda a: ('Element', 'Value'),
    'longlibefftabvalue': lambda a: ('Element', 'Value'),
    'liberrtab': lambda a: ('Value', 'Error condition'),
    'longliberrtab': lambda a: ('Value', 'Error condition'),
    'tokentable': lambda a: (a[2], a[3]),
    'lib2dtab2': lambda a: ('', '', a[2], a[3]),
}


# ---------------------------------------------------------------- conversion

class Ctx:
    """State of one output file."""
    def __init__(self, doc, sec):
        self.doc, self.sec = doc, sec
        self.pnum = 0
        self.notes = collections.Counter()
        self.foot = []
        self.defs = 0


MATH_SYM = {
    'leq': '≤', 'le': '≤', 'geq': '≥', 'ge': '≥', 'neq': '≠', 'ne': '≠', 'times': '×',
    'cdot': '·', 'infty': '∞', 'rightarrow': '→', 'to': '→', 'leftarrow': '←',
    'Rightarrow': '⇒', 'mapsto': '↦', 'land': '∧', 'lor': '∨', 'lnot': '¬', 'neg': '¬',
    'pm': '±', 'in': '∈', 'notin': '∉', 'subset': '⊂', 'cup': '∪', 'cap': '∩',
    'equiv': '≡', 'approx': '≈', 'sim': '∼', 'lfloor': '⌊', 'rfloor': '⌋', 'lceil': '⌈',
    'rceil': '⌉', 'ell': 'ℓ', 'bmod': ' mod ', 'alpha': 'α', 'beta': 'β', 'gamma': 'γ',
    'delta': 'δ', 'epsilon': 'ε', 'zeta': 'ζ', 'theta': 'θ', 'lambda': 'λ', 'mu': 'μ',
    'nu': 'ν', 'pi': 'π', 'rho': 'ρ', 'sigma': 'σ', 'phi': 'φ', 'Gamma': 'Γ', 'Pi': 'Π',
    'log': 'log', 'ln': 'ln', 'min': 'min', 'max': 'max', 'sin': 'sin', 'cos': 'cos',
    'exp': 'exp', 'prime': '′', 'sum': '∑', 'textmu': 'µ', 'lim': 'lim', 'vdots': '⋮', 'lvert': '|', 'rvert': '|', 'mid': '|',
    'langle': '⟨', 'rangle': '⟩', 'oplus': '⊕', 'forall': '∀', 'exists': '∃',
}
MATH_HARD = re.compile(r'\\(frac|sum|binom|sqrt|int|prod|begin|over|left|right|bar|hat|underbrace)\b|&')

FUNDESC = {
    'recommended': 'Recommended practice', 'required': 'Required behavior',
    'constraints': 'Constraints', 'mandates': 'Mandates', 'expects': 'Preconditions',
    'effects': 'Effects', 'ensures': 'Postconditions', 'returns': 'Returns',
    'throws': 'Throws', 'default': 'Default behavior', 'complexity': 'Complexity',
    'remarks': 'Remarks', 'errors': 'Error conditions', 'sync': 'Synchronization',
    'implimits': 'Implementation limits', 'replaceable': 'Replaceable',
    'returntype': 'Return type', 'cvalue': 'Value', 'ctype': 'Type', 'ctypes': 'Types',
    'dtype': 'Default type', 'ctemplate': 'Class template', 'templalias': 'Alias template',
    'requires': 'Requires', 'realnotes': 'Remarks',
}
DIFF = {'change': 'Change', 'rationale': 'Rationale', 'effect': 'Effect on original feature',
        'difficulty': 'Difficulty of converting', 'howwide': 'How widely used'}

CODE1 = ('tcode keyword cname libconcept deflibconcept libglobal idxcode texttt CodeStylex ecname '
         'exposid exposconcept defexposconcept exposconceptnc UNSP UNSPnc mathtt').split()
EM1 = ('grammarterm gterm fakegrammarterm grammartermnc regrammarterm term placeholder '
       'placeholdernc defn textit emph doccite cvqual EXPO Fundescx newoldconcept '
       'defnnewoldconcept textsl numconst mathit locgrammarterm').split()
ID1 = ('textrm textnormal textup text mbox textsf mathsf mathrm nocode hbox makebox logop '
       'ensuremath mathbin added uline descr hdstyle lhdr chdr rhdr colhdr textsuperscript '
       'mathscr mathcal operatorname rb rlap mathrel').split()
DROP = {  # name -> number of {} arguments dropped with it
    'indextext': 1, 'indexlibrary': 1, 'indexlibraryglobal': 1, 'indexlibraryctor': 1,
    'indexlibrarydtor': 1, 'indexlibraryzombie': 1, 'indexdefn': 1, 'indexheader': 1,
    'indexhdr': 1, 'indexgram': 1, 'indexgrammar': 1, 'indeximpldef': 1, 'indexconcept': 1,
    'index': 1, 'indexunordmem': 1, 'indexordmem': 1, 'indexlibrarymember': 2,
    'indexlibrarymemberexpos': 2, 'indexlibrarymemberx': 2, 'addxref': 1, 'hspace': 1,
    'vspace': 1, 'color': 1, 'phantom': 1, 'removed': 1, 'setlength': 2, 'enlargethispage': 1,
    'cline': 1, 'pagebreak': 0, 'nopagebreak': 0, 'nolinebreak': 0, 'linebreak': 0,
    'itcorr': 0, 'nocorr': 0, 'ungap': 0, 'noindent': 0, 'relax': 0, 'centering': 0,
    'small': 0, 'hfill': 0, 'hfil': 0, 'raggedright': 0, 'normalsize': 0, 'footnotesize': 0,
    'clearpage': 0, 'newpage': 0, 'columnbreak': 0, 'protect': 0, 'upshape': 0, 'itshape': 0,
    'bfseries': 0, 'rmfamily': 0, 'ttfamily': 0, 'sffamily': 0, 'normalfont': 0,
    'obeyspaces': 0, 'frenchspacing': 0, 'smaller': 0, 'larger': 0, 'brk': 0, 'topline': 0,
    'capsep': 0, 'rowsep': 0, 'hline': 0, 'bottomline': 0, 'endhead': 0, 'endfirsthead': 0,
    'continuedcaption': 0, 'displaystyle': 0, 'big': 0, 'bigl': 0, 'bigr': 0, 'Big': 0,
    'Bigl': 0, 'Bigr': 0, 'kill': 0, 'onelineskip': 0, 'tiny': 0, 'scriptsize': 0,
    'nonfrenchspacing': 0, 'bnfindentfirst': 0, 'vfill': 0, 'textsmaller': 0, 'bigskip': 0,
    'medskip': 0, 'smallskip': 0, 'microtypesetup': 1, 'nobreak': 0, 'BnfNontermshape': 0, 'hphantom': 1, 'sloppy': 0, 'fussy': 0, 'left': 0, 'right': 0,
}
CONST = {
    'Cpp': 'C++', 'CppIII': 'C++ 2003', 'CppXI': 'C++ 2011', 'CppXIV': 'C++ 2014',
    'CppXVII': 'C++ 2017', 'CppXX': 'C++ 2020', 'dcr': '--', 'caret': '^',
    'textasciitilde': '~', 'textasciicircum': '^', 'atsign': '@', 'unun': '__',
    'commentellip': '/* ... */', 'ntbs': 'NTBS', 'ntmbs': 'NTMBS', 'quad': ' ', 'qquad': ' ',
    'space': ' ', 'copyright': '©', 'textregistered': '®', 'S': '§', 'textsection': '§',
    'textbar': '|', 'textgreater': '>', 'textquotesingle': "'", 'textquotedbl': '"',
    'ldots': '…', 'dotsc': '…', 'dots': '…', 'dotsb': '…', 'cdots': '…', 'lbrace': '{',
    'rbrace': '}', 'bnfindent': '    ', 'bitand': ' bitand ', 'bitor': ' bitor ',
    'xor': ' xor ', 'rightshift': ' rshift ', 'opt': 'ₒₚₜ', 'textdollar': '$', 'ss': 'ß',
    'o': 'ø', 'TeX': 'TeX', 'LaTeX': 'LaTeX', 'textemdash': '—', 'textendash': '–',
}
EMCODE = {  # italic code-ish words
    'seebelow': 'see below', 'seebelownc': 'see below', 'unspec': 'unspecified',
    'unspecnc': 'unspecified', 'unspecbool': 'unspecified-bool-type',
    'unspecuniqtype': 'unspecified unique type', 'unspecalloctype': 'unspecified allocator type',
    'expos': 'exposition only', 'impdef': 'implementation-defined',
    'impdefnc': 'implementation-defined', 'notdef': 'not defined',
}
ACCENT = {'"': '\u0308', "'": '\u0301', '`': '\u0300', '^': '\u0302', '~': '\u0303',
          'c': '\u0327', 'v': '\u030c', 'u': '\u0306', 'H': '\u030b', '=': '\u0304'}
TEXT_RUN = re.compile(r"[^\\{}~$\n`'\-*_<^&]+")


class Conv:
    def __init__(self, ctx, plain=False, inline=False, bnf=False, math=False):
        self.ctx, self.plain, self.inline, self.bnf, self.math = ctx, plain, inline, bnf, math
        self.blocks = []
        self.cur = []
        self.pending = None

    # -- output
    def add(self, t):
        self.cur.append(t)

    def flush(self):
        t = re.sub(r'\s+', ' ', ''.join(self.cur)).strip()
        t = re.sub(r'(<br>\s*)+$', '', re.sub(r'^(<br>\s*)+', '', t))
        t = re.sub(r' ([,.;:])(?= |$)', r'\1', t)
        self.cur = []
        if t:
            if self.pending:
                t = self.pending + ' ' + t
                self.pending = None
            self.blocks.append(t)

    def block(self, b):
        self.flush()
        if self.pending:
            self.blocks.append(self.pending)
            self.pending = None
        self.blocks.append(b)

    def text(self):
        self.flush()
        return '<br>'.join(inline_block(b) for b in self.blocks)

    def sub(self, **kw):
        a = dict(plain=self.plain, bnf=self.bnf, math=self.math)
        a.update(kw)
        return Conv(self.ctx, **a)

    def inl(self, s, **kw):
        c = self.sub(inline=True, **kw)
        c.run(s)
        return c.text()

    def pl(self, s):
        return self.inl(s, plain=True, math=False)

    def em(self, t):
        t = t.strip()
        if self.plain or not t:
            return t
        return '*' + t + '*'

    def code(self, t):
        t = re.sub(r'\s+', ' ', t).strip()
        if self.plain or not t:
            return t
        if '`' in t:
            return '`` ' + t + ' ``'
        return '`' + t + '`'

    def esc(self, t):
        if self.plain:
            return t
        return t.replace('*', '\\*').replace('_', '\\_').replace('<', '&lt;')

    # -- references
    def link(self, label, text=None):
        doc = self.ctx.doc
        label = label.strip()
        if '\\' in label:
            label = self.pl(label)
        ent = doc.by_label.get(label)
        shown = text or '[' + label + ']'
        if self.plain:
            return shown
        if not ent:
            warnings['unresolved reference ' + label] += 1
            return self.esc(shown)
        sec, anchor = ent
        here = os.path.dirname(self.ctx.sec.path)
        url = os.path.relpath(sec.path, here or '.')
        if anchor:
            url += '#' + anchor
        return '[%s](%s)' % (shown.replace('_', '\\_'), url)

    def refs(self, a):
        return ', '.join(self.link(x) for x in a.split(',') if x.strip())

    # -- the walker
    def run(self, s):
        i, n = 0, len(s)
        while i < n:
            m = TEXT_RUN.match(s, i)
            if m:
                t = m.group()
                if self.math and not self.plain:
                    t = re.sub(r'[A-Za-z]+', lambda k: '*' + k.group() + '*', t)
                    self.add(t)
                else:
                    self.add(self.esc(t))
                i = m.end()
                continue
            c = s[i]
            if c == '\\':
                m = NAME.match(s, i + 1)
                if not m:
                    i = self.symbol(s, i + 1)
                    continue
                name, i = m.group(), m.end()
                if name == 'begin':
                    env, i = read_group(s, i)
                    i = self.env(env, s, i)
                elif name == 'end':
                    _, i = read_group(s, i)
                else:
                    i = self.macro(name, s, i)
            elif c == '{':
                g, i = read_group(s, i)
                self.run(g)
            elif c == '}':
                i += 1
            elif c == '~':
                self.add(' ')
                i += 1
            elif c == '$':
                j = i + 1
                while j < n and s[j] != '$':
                    j += 2 if s[j] == '\\' else 1
                self.add(self.mathspan(s[i + 1:j]))
                i = j + 1
            elif c == '\n':
                m = re.compile(r'\n[ \t]*\n\s*').match(s, i)
                if m and not self.inline:
                    self.flush()
                    i = m.end()
                else:
                    self.add(' ')
                    i += 1
            elif c in '`\'-' and not self.plain and not self.math:
                if s.startswith('``', i):
                    self.add('“')
                    i += 2
                elif s.startswith("''", i):
                    self.add('”')
                    i += 2
                elif s.startswith('---', i):
                    self.add('—')
                    i += 3
                elif s.startswith('--', i):
                    self.add('–')
                    i += 2
                else:
                    self.add({'`': '‘', "'": '’', '-': '-'}[c])
                    i += 1
            elif c in '^_' and self.math:
                a, i = read_arg(s, i + 1)
                t = self.inl(a)
                if self.plain:
                    self.add(c + (t if len(t) == 1 else '(' + t + ')'))
                else:
                    self.add('<sup>%s</sup>' % t if c == '^' else '<sub>%s</sub>' % t)
            else:
                self.add(self.esc(c))
                i += 1

    def symbol(self, s, i):
        ch = s[i:i + 1]
        i += 1
        if ch == '\\':
            if s[i:i + 1] == '*':
                i += 1
            _, i = read_opt(s, i)
            self.add(' ' if self.plain else '<br>')
        elif ch in '{}%&#$_':
            self.add(self.esc(ch) if ch != '#' else '#')
        elif ch in ' \n':
            self.add(' ')
        elif ch == '~':
            self.add('~')
        elif ch in ';:>':
            self.add(' ')
        elif ch == '[':
            j = s.find('\\]', i)
            self.block('$$' + s[i:j].strip() + '$$')
            i = j + 2
        elif ch in ACCENT and ch not in ',':
            a, i = read_arg(s, i)
            self.add(unicodedata.normalize('NFC', a + ACCENT[ch]))
        # \, \/ \- \! \@ and anything else: nothing
        return i

    def mathspan(self, t):
        t = t.strip()
        if MATH_HARD.search(t):
            if self.plain:
                return t
            return '$' + re.sub(r'\\tcode\b', r'\\texttt', t) + '$'
        return self.inl(t, math=True)

    def macro(self, name, s, i):
        zero = lambda: skip_inline_ws(s, i)
        if name in CODE1:
            a, i = read_arg(s, i)
            if self.bnf and name in ('tcode', 'keyword'):
                self.add("'" + self.pl(a) + "'")
            else:
                self.add(self.code(self.pl(a)))
        elif name in EM1:
            a, i = read_arg(s, i)
            self.add(self.em(self.inl(a, math=False)))
        elif name in ID1:
            _, i = read_opt(s, i)
            a, i = read_arg(s, i)
            self.add(self.inl(a, math=False) if name.startswith('text') or name == 'mbox' else self.inl(a))
        elif name in user_macros:
            nargs, body = user_macros[name]
            for k in range(nargs):
                a, i = read_arg(s, i)
                body = re.sub(r'(\\[a-zA-Z]+)#%d' % (k + 1), lambda m: m.group(1) + ' ' + a, body)
                body = body.replace('#%d' % (k + 1), a)
            if not nargs:
                i = zero() if s[i:i + 2] != '{}' else i
            self.run(body)
        elif name in DROP:
            if s[i:i + 1] == '*':
                i += 1
            _, i = read_opt(s, i)
            for _ in range(DROP[name]):
                _, i = read_arg(s, i)
            if not DROP[name]:
                i = zero()
        elif name in CONST:
            v = CONST[name]
            if name == 'opt':
                a, i = read_arg(s, i)
                v = self.inl(a) + v
            elif self.plain and v == '…':
                v = '...'
                i = zero()
            else:
                i = zero()
            self.add(v)
        elif name in EMCODE:
            self.add(self.em(EMCODE[name]))
            i = zero()
        elif name in FUNDESC:
            self.add(self.em(FUNDESC[name] + ':') + ' ')
            i = zero()
        elif name in DIFF:
            self.add('<br>**%s:** ' % DIFF[name])
            i = zero()
        elif name == 'VERB':
            a, i = read_arg(s, i)
            self.verb(*verbs[int(a)])
        elif name == 'pnum':
            self.pnum()
            i = zero()
        elif name in ('ref', 'iref', 'tref', 'fref', 'diffref', 'diffrefs'):
            a, i = read_arg(s, i)
            doc = self.ctx.doc
            if name == 'ref':
                self.add(self.refs(a))
            elif name == 'iref':
                self.add(' (' + self.refs(a) + ')')
            elif name == 'tref':
                self.add(self.link('tab:' + a, 'Table %s' % doc.tables.get(a, '?')))
            elif name == 'fref':
                self.add(self.link('fig:' + a, 'Figure %s' % doc.figures.get(a, '?')))
            else:
                self.pnum()
                self.add('**Affected subclause%s:** %s' % ('s' if ',' in a else '', self.refs(a)))
        elif name == 'nodiffref':
            self.pnum()
            i = zero()
        elif name == 'footnote':
            a, i = read_arg(s, i)
            if self.plain:
                return i
            c = Conv(self.ctx)
            c.run(a)
            self.ctx.foot.append(c.text())
            self.add('[^%d]' % len(self.ctx.foot))
        elif name == 'definition':
            term, i = read_arg(s, i)
            lab, i = read_arg(s, i)
            self.ctx.defs += 1
            self.ctx.notes.clear()
            self.block('<a id="%s"></a>\n### %s.%d %s [%s]' % (
                lab, self.ctx.sec.num, self.ctx.defs, self.inl(term), lab))
        elif name == 'defncontext':
            a, i = read_arg(s, i)
            self.add('⟨' + self.inl(a) + '⟩ ')
        elif name == 'label':
            a, i = read_arg(s, i)
            if not self.plain:
                self.add('<a id="%s"></a>' % a)
        elif name in ('defnx', 'libmember', 'libconceptx', 'exposconceptx'):
            a, i = read_arg(s, i)
            _, i = read_arg(s, i)
            self.add(self.em(self.inl(a)) if name == 'defnx' else self.code(self.pl(a)))
        elif name in ('defnadj', 'defnadjx'):
            a, i = read_arg(s, i)
            b, i = read_arg(s, i)
            if name == 'defnadjx':
                _, i = read_arg(s, i)
            self.add(self.em(self.inl(a) + ' ' + self.inl(b)))
        elif name in ('oldconcept', 'defnoldconcept', 'oldconceptname'):
            a, i = read_arg(s, i)
            t = 'Cpp17' + self.pl(a)
            self.add(t if name == 'oldconceptname' else self.em(t))
        elif name == 'textbf':
            a, i = read_arg(s, i)
            t = self.inl(a).strip()
            self.add(t if self.plain or not t else '**' + t + '**')
        elif name in ('NTS', 'textsc'):
            a, i = read_arg(s, i)
            self.add(self.inl(a).upper() if name == 'NTS' else self.inl(a))
        elif name in ('libheader', 'libheaderdef', 'libnoheader', 'libheaderref', 'libheaderrefx',
                      'libdeprheaderref'):
            a, i = read_arg(s, i)
            self.add(self.code('<' + self.pl(a) + '>'))
            if name == 'libheaderrefx':
                b, i = read_arg(s, i)
                self.add(' (' + self.link(b) + ')')
            elif name == 'libheaderref':
                self.add(' (' + self.link(a + '.syn') + ')')
            elif name == 'libdeprheaderref':
                self.add(' (' + self.link('depr.' + a + '.syn') + ')')
        elif name in ('xname', 'defnxname', 'defnlibxname', 'mname'):
            a, i = read_arg(s, i)
            self.add(self.code('__' + self.pl(a) + ('__' if name == 'mname' else '')))
        elif name in ('impldef', 'impldefplain', 'impdefx'):
            _, i = read_arg(s, i)
            self.add('implementation-defined' if name != 'impdefx' else self.em('implementation-defined'))
        elif name == 'cv':
            self.add(self.em('cv'))
            i = zero() if s[i:i + 2] != '{}' else i
        elif name in ('range', 'crange', 'brange', 'orange'):
            a, i = read_arg(s, i)
            b, i = read_arg(s, i)
            l, r = {'range': '[)', 'crange': '[]', 'brange': '(]', 'orange': '()'}[name]
            self.add(self.code(l + self.pl(a) + ', ' + self.pl(b) + r))
        elif name == 'countedrange':
            a, i = read_arg(s, i)
            b, i = read_arg(s, i)
            self.add(self.code(self.pl(a)) + ' + ' + self.code('[0, ' + self.pl(b) + ')'))
        elif name == 'state':
            a, i = read_arg(s, i)
            b, i = read_arg(s, i)
            self.add(self.code(self.pl(a)) + ('_' + self.pl(b) if self.plain else '<sub>%s</sub>' % self.inl(b)))
        elif name == 'bigoh':
            a, i = read_arg(s, i)
            self.add('𝑂(' + self.inl(a, math=True) + ')')
        elif name == 'leftshift':
            a, i = read_arg(s, i)
            self.add(' lshift' + ('<sub>%s</sub> ' % self.inl(a) if not self.plain else '_' + self.pl(a) + ' '))
        elif name == 'Fundesc' or name == 'diffhead':
            a, i = read_arg(s, i)
            self.add(self.em(self.inl(a) + ':') + ' ' if name == 'Fundesc' else '**%s:** ' % self.inl(a))
        elif name == 'diffdef':
            a, i = read_arg(s, i)
            self.add('<br>**%s:** ' % self.inl(a))
        elif name == 'effectafteritemize':
            self.add('**Effect on original feature:** ')
        elif name in ('xref', 'xrefc'):
            self.add(self.em('See also:') + ' ')
            if name == 'xrefc':
                a, i = read_arg(s, i)
                self.add('ISO C ' + self.inl(a))
            else:
                i = zero()
        elif name in ('nontermdef', 'renontermdef', 'locnontermdef'):
            a, i = read_arg(s, i)
            self.add(self.pl(a) + ':')
        elif name == 'terminal':
            a, i = read_arg(s, i)
            t = self.pl(a)
            self.add("'" + t + "'" if self.bnf else self.code(t))
        elif name in ('textbackslash', 'backslash'):
            self.add('\\' if self.plain else '\\\\')
            i = zero()
        elif name == 'textunderscore':
            self.add(self.esc('_'))
            i = zero()
        elif name == 'textless':
            self.add(self.esc('<'))
            i = zero()
        elif name in ('br', 'newline', 'break'):
            self.add(' ' if self.plain else '<br>')
            i = zero()
        elif name == 'par':
            if not self.inline:
                self.flush()
            i = zero()
        elif name in ('kern', 'penalty', 'vskip', 'hskip'):
            m = re.compile(r'\s*-?[\d.]*\s*(?:pt|em|ex|in|cm|mm|\\\w+)?').match(s, i)
            i = m.end()
        elif name in ('textcolor', 'raisebox'):
            _, i = read_arg(s, i)
            _, i = read_opt(s, i)
            a, i = read_arg(s, i)
            self.add(self.inl(a))
        elif name == 'ohdrx':
            _, i = read_arg(s, i)
            a, i = read_arg(s, i)
            self.add(self.inl(a))
        elif name in ('noteintro', 'noteoutro'):
            a, i = read_arg(s, i)
            self.add('[*%s*: ' % self.inl(a) if name == 'noteintro' else '— *end %s*]' % self.inl(a))
        elif name == 'multicolumn':
            _, i = read_arg(s, i)
            _, i = read_arg(s, i)
            a, i = read_arg(s, i)
            self.add(self.inl(a))
        elif name == 'url':
            a, i = read_arg(s, i)
            self.add(a if self.plain else '<' + a + '>')
        elif name == 'href':
            u, i = read_arg(s, i)
            a, i = read_arg(s, i)
            self.add('[%s](%s)' % (self.inl(a), u))
        elif name == 'ednote':
            a, i = read_arg(s, i)
            self.add('[*Editor’s note*: %s]' % self.inl(a))
        elif name == 'item':
            self.add('<br>• ')
        elif name in ('c', 'v', 'u', 'H') and s[i:i + 1] == '{':
            a, i = read_arg(s, i)
            self.add(unicodedata.normalize('NFC', a + ACCENT[name]))
        elif name in MATH_SYM:
            self.add(MATH_SYM[name])
            if s[i:i + 1] == ' ' and s[i + 1:i + 2].isalnum() and len(MATH_SYM[name]) > 1:
                self.add(' ')
        else:
            unknown['\\' + name] += 1
        return i

    def pnum(self):
        if self.inline:
            return
        self.flush()
        if self.pending:
            self.blocks.append(self.pending)
        self.ctx.pnum += 1
        self.pending = '**%d**' % self.ctx.pnum

    # -- environments
    def env(self, env, s, i):
        doc = self.ctx.doc
        if env in ('note', 'example', 'defnote'):
            opt, i = read_opt(s, i)
            inner, j = find_end(s, i, env)
            kind = 'example' if env == 'example' else 'note'
            self.ctx.notes[kind] += 1
            head = '%s %d' % (opt or kind.capitalize(), self.ctx.notes[kind])
            if env == 'defnote':
                head += ' to entry'
            self.add('[*%s*: ' % head)
            self.run(inner)
            self.add(' — *end %s*] ' % kind)
            return j
        if env in LIST_ENVS:
            _, i = read_opt(s, i)
            inner, j = find_end(s, i, env)
            self.list(env, inner)
            return j
        if env in BNF_ENVS:
            inner, j = find_end(s, i, env)
            self.bnfblock(inner)
            return j
        if env in PASS_ENVS:
            _, i = read_opt(s, i)
            for _ in range(PASS_ENVS[env]):
                _, i = read_arg(s, i)
            inner, j = find_end(s, i, env)
            self.run(inner)
            return j
        if env == 'removedblock':
            return find_end(s, i, env)[1]
        if env == 'importgraphic':
            args, i = read_args(s, i, 3)
            inner, j = find_end(s, i, env)
            b = '<a id="fig:%s"></a>\n**Figure %s: %s** [fig:%s]' % (
                args[1], doc.figures.get(args[1], '?'), self.inl(args[0]), args[1])
            dot = os.path.join(doc.src, args[2].replace('.pdf', '.dot'))
            if os.path.exists(dot):
                b += '\n\n```dot\n' + open(dot).read().strip() + '\n```'
            self.block(b)
            return j
        if env in doc.table_args:
            args, i = read_args(s, i, doc.table_args[env])
            inner, j = find_end(s, i, env)
            self.table(env, args, inner)
            return j
        if env in ('eqnarray*', 'eqnarray', 'displaymath', 'equation*', 'align*'):
            inner, j = find_end(s, i, env)
            self.block('$$\\begin{aligned}' + inner.strip() + '\\end{aligned}$$')
            return j
        if env == 'tabbing':
            inner, j = find_end(s, i, env)
            rows = [self.inl(re.sub(r'\\[=>]', ' ', r)) for r in split_top(inner)]
            self.block('<br>'.join(r for r in rows if r.strip()))
            return j
        unknown['env ' + env] += 1
        inner, j = find_end(s, i, env)
        self.run(inner)
        return j

    def list(self, env, inner):
        out, multi = [], False
        for k, (lab, body) in enumerate(split_items(inner)):
            c = Conv(self.ctx)
            c.run(body)
            c.flush()
            blocks = c.blocks or ['']
            if lab is not None:
                head = self.inl(lab)
                if env == 'description' or head.endswith(':'):
                    head = '**' + head + '**'
                blocks[0] = (head + ' ' + blocks[0]).strip()
            mark = '%d. ' % (k + 1) if env == 'enumerate' and lab is None else '- '
            if len(blocks) > 1:
                multi = True
            t = '\n\n'.join(blocks)
            pad = ' ' * len(mark)
            out.append(mark + ('\n'.join(pad + l if l else l for l in t.split('\n')))[len(pad):])
        if out:
            self.block(('\n\n' if multi else '\n').join(out))

    def verb(self, env, title, body):
        if env == 'verb':
            self.add(self.code(body))
            return
        lines = body.expandtabs(8).split('\n')
        res = []
        for l in lines:
            if env != 'outputblock':
                # comments are TeX (texcl): a trailing % and index lines are not code
                if re.match(r'\s*\\index\w*\{.*\}%?\s*$', l):
                    continue
                if '//' in l and l.rstrip().endswith('%'):
                    l = l.rstrip()[:-1]
                l = re.sub(r'@([^@\n]*)@', lambda m: self.pl(m.group(1)), l)
                k = l.find('//')
                if k >= 0 and re.search(r'[\\$~{]', l[k:]):
                    l = l[:k] + '//' + self.codecomment(l[k + 2:])
            res.append(l.rstrip())
        while res and not res[0].strip():
            res.pop(0)
        while res and not res[-1].strip():
            res.pop()
        ind = min((len(l) - len(l.lstrip()) for l in res if l.strip()), default=0)
        res = [l[ind:] for l in res]
        if title:
            self.block('*%s:*' % self.inl(title))
        self.block('```%s\n%s\n```' % ('text' if env == 'outputblock' else 'cpp', '\n'.join(res)))

    def codecomment(self, t):
        lead = t[:len(t) - len(t.lstrip())]
        return lead + self.pl(t.strip())

    def bnfblock(self, inner):
        res = []
        for l in inner.split('\n'):
            l = l.replace('\\br', ' ').rstrip()
            if not l.strip():
                res.append('')
                continue
            lead = l[:len(l) - len(l.lstrip())].replace('\t', '    ')
            c = self.sub(inline=True, plain=True, bnf=True, math=False)
            c.run(l.strip())
            res.append(lead + ''.join(c.cur).rstrip())
        while res and not res[0]:
            res.pop(0)
        while res and not res[-1]:
            res.pop()
        self.block('```bnf\n' + '\n'.join(res) + '\n```')

    def table(self, env, args, inner):
        doc = self.ctx.doc
        cap, lab = table_caption(env, args)
        if env in ('tabular', 'longtable', 'array', 'table'):
            cap = lab = None
        if '\\endhead' in inner:
            k = inner.find('\\endfirsthead')
            head = inner[:k if k >= 0 else inner.find('\\endhead')]
            inner = head + inner[inner.find('\\endhead') + 8:]
        inner = re.sub(r'\\(begin|end)\{mcftcol\}|\\columnbreak\b', '', inner)
        # a row ruled off by \cline only in some columns continues the cell above in the others
        ruled = []
        if env != 'lib2dtab2':
            for r in split_top(inner):
                lead = re.match(r'(\s|\\(topline|capsep|rowsep|hline|bottomline)\b|\\cline\{[^}]*\})*', r).group()
                cl = re.findall(r'\\cline\{(\d+)-(\d+)\}', lead)
                if not r[len(lead):].strip():
                    continue
                full = re.search(r'\\(topline|capsep|rowsep|hline)\b', lead)
                ruled.append(None if full or not cl else
                             {c for a, b in cl for c in range(int(a) - 1, int(b))})
        inner = re.sub(r'\\(topline|capsep|rowsep|hline|bottomline|endhead|endfirsthead|continuedcaption)\b',
                       '', inner)
        inner = re.sub(r'\\cline\{[^}]*\}', '', inner)
        header = None
        if env in TABLE_HEADERS:
            header = [self.inl(h) for h in TABLE_HEADERS[env](args)]
        rows = []
        for r in split_top(inner):
            if not r.strip():
                continue
            ishead = re.search(r'\\(lhdr|chdr|rhdr|hdstyle|colhdr)\b', r) and header is None and not rows
            cells = []
            for cell in split_top(r, amp=True):
                c = Conv(self.ctx)
                c.run(cell)
                cells.append(c.text().replace('|', '\\|').replace('\n', ' '))
            if ishead:
                header = cells
            else:
                rows.append(cells)
        width = max([len(r) for r in rows] + [len(header or [])] + [1])
        if len(ruled) == len(rows) + (1 if header and env not in TABLE_HEADERS else 0) and any(ruled):
            ruled = ruled[len(ruled) - len(rows):]
            for col in range(width):
                k = 0
                while k < len(rows):
                    e = k + 1
                    while e < len(rows) and ruled[e] is not None and col not in ruled[e]:
                        e += 1
                    if e - k > 1:
                        txt = ' '.join(r[col] for r in rows[k:e] if col < len(r) and r[col].strip())
                        for r in rows[k:e]:
                            r.extend([''] * (width - len(r)))
                            r[col] = txt
                    k = e
        header = (header or []) + [''] * (width - len(header or []))
        md = ['| ' + ' | '.join(header) + ' |', '|' + ' --- |' * width]
        for r in rows:
            md.append('| ' + ' | '.join(r + [''] * (width - len(r))) + ' |')
        b = '\n'.join(md)
        if cap is not None:
            b = '<a id="tab:%s"></a>\n**Table %s: %s** [tab:%s]\n\n%s' % (
                lab, doc.tables.get(lab, '?'), self.inl(cap), lab, b)
        self.block(b)


def skip_inline_ws(s, i):
    """After a macro without arguments TeX skips spaces (and one newline)."""
    n = len(s)
    while i < n and s[i] in ' \t':
        i += 1
    if i < n and s[i] == '\n' and not re.match(r'\n[ \t]*\n', s[i:]):
        i += 1
        while i < n and s[i] in ' \t':
            i += 1
    return i


def inline_block(b):
    """A block squeezed into one line (table cells, footnotes)."""
    if b.startswith('```'):
        lines = b.split('\n')[1:-1]
        return '<br>'.join('`` ' + l + ' ``' if '`' in l else '`' + l + '`' for l in lines if l.strip())
    return b.replace('\n\n', '<br>').replace('\n', '<br>')


# ---------------------------------------------------------------- output

def title_of(doc, sec, here):
    c = Conv(Ctx(doc, here), inline=True)
    c.run(sec.title)
    return c.text()


def sec_link(doc, sec, here):
    url = os.path.relpath(sec.path, os.path.dirname(here.path) or '.')
    kind = 'Annex ' if sec.annex else ''
    return '[%s%s %s \\[%s\\]](%s)' % (kind, sec.num, title_of(doc, sec, here), sec.label.replace('_', '\\_'), url)


def toc(doc, sec, here, depth, out):
    out.append('  ' * depth + '- ' + sec_link(doc, sec, here))
    for k in sec.kids:
        toc(doc, k, here, depth + 1, out)


def render(doc, sec, k):
    ctx = Ctx(doc, sec)
    c = Conv(ctx)
    c.run(sec.body)
    c.flush()
    if c.pending:
        c.blocks.append(c.pending)
    kind = {'infannex': 'Annex %s (informative) ', 'normannex': 'Annex %s (normative) '}.get(sec.annex, '%s ') % sec.num
    out = ['# %s%s [%s]' % (kind, title_of(doc, sec, sec), sec.label)]
    root = os.path.relpath('README.md', os.path.dirname(sec.path) or '.')
    nav = ['[Contents](%s)' % root]
    if sec.parent:
        nav.append('up: ' + sec_link(doc, sec.parent, sec))
    if k > 0:
        nav.append('prev: ' + sec_link(doc, doc.secs[k - 1], sec))
    if k + 1 < len(doc.secs):
        nav.append('next: ' + sec_link(doc, doc.secs[k + 1], sec))
    out.append(' · '.join(nav))
    out.extend(c.blocks)
    if sec.kids:
        t = ['## Subclauses', '']
        for kid in sec.kids:
            toc(doc, kid, sec, 0, t)
        out.append('\n'.join(t))
    if ctx.foot:
        out.append('\n'.join('[^%d]: %s' % (n + 1, f) for n, f in enumerate(ctx.foot)))
    return '\n\n'.join(out) + '\n'


README = """# C++ standard working draft {tag}, split by subclause

Generated by `bench/cxx-spec.py --tag {tag}` from the LaTeX sources of
<https://github.com/cplusplus/draft>; do not edit by hand, rerun the script.
{tag} is {what}.

- One file per subclause, named by its stable name: `[expr.add]` is
  [expr.add.md](expr.add.md).  The library clauses ({libfirst} to {liblast})
  are under [lib/](lib/README.md): `[vector]` is `lib/vector.md`.
- A file holds the subclause's own paragraphs (**1**, **2**, ... as numbered in
  the standard, so `[expr.add]/4` is paragraph **4** of `expr.add.md`), then
  the list of its subclauses.  The first line links to the parent and to the
  previous and next subclause in document order.
- Cross references are links: `[basic.lookup]`.  Tables and figures keep their
  numbers and have anchors `#tab:NAME`, `#fig:NAME`; the terms of
  [intro.defs](intro.defs.md) have an anchor each.
- Grammar is in `bnf` blocks: terminals are quoted (`'class'`), nonterminals
  are bare, `ₒₚₜ` marks an optional symbol.  In running text nonterminals are
  *italic*.  [grammar-index.md](grammar-index.md) lists every nonterminal with
  the subclause that defines it; [gram](gram.md) is the standard's own summary.
- Notes and examples are numbered within their subclause and bracketed:
  [*Note 1*: ... — *end note*].  Footnotes are at the end of the file.
- {nsec} subclauses, {ncore} of them outside the library.

## Contents

{toc}
"""


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w') as f:
        f.write(text)


def generate(doc, out, tag):
    for d in (out, os.path.join(out, 'lib')):
        if os.path.isdir(d):
            for f in os.listdir(d):
                if f.endswith('.md'):
                    os.unlink(os.path.join(d, f))
    for k, s in enumerate(doc.secs):
        write(os.path.join(out, s.path), render(doc, s, k))
    root = Sec(0, 'README', '', '', False)
    libroot = Sec(0, 'README', '', '', True)
    # keep document order of the top level: language clauses, library, annexes
    top = []
    for s in doc.secs:
        if s.parent is None:
            if s.lib:
                top.append('- ' + sec_link(doc, s, root))
            else:
                toc(doc, s, root, 0, top)
    what = {'n4861': 'the final working draft of C++20 (ISO/IEC 14882:2020)',
            'n4950': 'the final working draft of C++23 (ISO/IEC 14882:2024)',
            'n4659': 'the final working draft of C++17 (ISO/IEC 14882:2017)'}.get(tag, 'a working draft')
    libsecs = [s for s in doc.secs if s.lib and s.parent is None]
    write(os.path.join(out, 'README.md'), README.format(
        tag=tag, what=what, toc='\n'.join(top), nsec=len(doc.secs),
        ncore=sum(1 for s in doc.secs if not s.lib),
        libfirst='`[%s]`' % libsecs[0].label, liblast='`[%s]`' % libsecs[-1].label))
    t = []
    for s in libsecs:
        toc(doc, s, libroot, 0, t)
    write(os.path.join(out, 'lib', 'README.md'),
          '# Library clauses\n\n[Contents](../README.md)\n\n' + '\n'.join(t) + '\n')
    g = ['# Grammar nonterminals', '', '[Contents](README.md)', '',
         'Each nonterminal and the subclause whose `bnf` block defines it.', '']
    for nt in sorted(doc.nonterms):
        g.append('- *%s*: %s' % (nt, ', '.join(
            '[[%s]](%s)' % (s.label, s.path) for s in doc.nonterms[nt])))
    write(os.path.join(out, 'grammar-index.md'), '\n'.join(g) + '\n')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--tag', default='n4861', help='tag of cplusplus/draft (n4861 = C++20)')
    ap.add_argument('--src', help='an existing checkout of cplusplus/draft at that tag')
    ap.add_argument('--out', default=os.path.join(os.path.dirname(__file__), '..', 'docs', 'cxx-spec'))
    ap.add_argument('-v', action='store_true', help='list unknown macros and unresolved references')
    a = ap.parse_args()
    tmp = None
    src = a.src
    if not src:
        tmp = tempfile.mkdtemp(prefix='cxx-draft-')
        subprocess.check_call(['git', 'clone', '-q', '--depth', '1', '--branch', a.tag,
                               'https://github.com/cplusplus/draft', tmp])
        src = tmp
    try:
        doc = load(Doc(), os.path.join(src, 'source'))
        generate(doc, os.path.normpath(a.out), a.tag)
    finally:
        if tmp:
            shutil.rmtree(tmp, ignore_errors=True)
    print('%d subclauses (%d library), %d tables, %d figures, %d nonterminals -> %s' % (
        len(doc.secs), sum(1 for s in doc.secs if s.lib), len(doc.tables), len(doc.figures),
        len(doc.nonterms), os.path.normpath(a.out)))
    print('unknown macros: %d kinds, unresolved references: %d' % (
        len(unknown), sum(1 for w in warnings if w.startswith('unresolved'))))
    if a.v:
        for k, n in unknown.most_common():
            print('  %5d %s' % (n, k))
        for k, n in sorted(warnings.items()):
            print('  %5d %s' % (n, k))
    return 0


if __name__ == '__main__':
    sys.exit(main())
