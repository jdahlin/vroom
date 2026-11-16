#!/usr/bin/env python3
"""The standard's examples as test files for the C++ parser
(doc/PLAN-cxx-parse.md, V2).

  bench/cxx-spec-examples.py [--write] LABEL...     one subclause each
  bench/cxx-spec-examples.py --list [CLAUSE]       examples per subclause

Each code block of an example in docs/cxx-spec/CLAUSE/LABEL.md becomes
src/test/cxx/parse/spec/LABEL/N.cpp, N counting the subclause's examples
from 1 (the standard's own numbering).  An example is a translation unit of
its own: concatenated, the examples of a subclause redeclare each other's
names, and inside a namespace they would mean something else.

A file starts with where it is from and two lines to fill in by hand once
the step that parses it is written:

    // [dcl.ambig.res] Example 1 (paragraph 1), docs/cxx-spec/dcl/dcl.ambig.res.md
    // needs: P1
    // status: unannotated

and every line the standard marks as an error ("// error", "ill-formed")
gets one of `// syntax-error` (the parser must report one on this line) or
`// semantic` (ill-formed for a reason the parser does not check: it must
accept the line) appended, replacing status: unannotated.

Without --write, it prints what it would write; an existing file is never
overwritten (it has hand annotations), the difference is reported instead.
"""
import argparse
import glob
import os
import re
import sys

R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SPEC = os.path.join(R, 'docs', 'cxx-spec')
OUT = os.path.join(R, 'src', 'test', 'cxx', 'parse', 'spec')


def subclause_path(label):
    found = glob.glob(os.path.join(SPEC, '*', label + '.md'))
    if not found:
        sys.exit(f'cxx-spec-examples: no subclause [{label}] in docs/cxx-spec')
    return found[0]


def examples(path):
    """(number, paragraph, code) for each code block inside an example"""
    text = open(path, encoding='utf-8').read()
    out = []
    paragraph = 0
    number = None
    in_example = False
    lines = text.split('\n')
    i = 0
    while i < len(lines):
        line = lines[i]
        m = re.match(r'^\*\*(\d+)\*\*', line)
        if m:
            paragraph = int(m.group(1))
        for m in re.finditer(r'\[\*Example(?: (\d+))?\*:|— \*end example\*\]', line):
            if m.group(0).startswith('[*Example'):
                in_example = True
                number = int(m.group(1) or 1)
            else:
                in_example = False
        if line.startswith('```cpp') and in_example:
            j = i + 1
            code = []
            while j < len(lines) and not lines[j].startswith('```'):
                code.append(lines[j])
                j += 1
            out.append((number, paragraph, '\n'.join(code)))
            i = j
        i += 1
    return out


ERROR_COMMENT = re.compile(r'//.*\b(error|ill-formed)\b', re.I)


def render(label, path, number, paragraph, code, part):
    rel = os.path.relpath(path, R)
    name = f'Example {number}'
    if part:
        name += f', part {part}'
    head = [f'// [{label}] {name} (paragraph {paragraph}), {rel}',
            '// needs: (the step whose grammar parses it)']
    if any(ERROR_COMMENT.search(l) for l in code.split('\n')):
        head.append('// status: unannotated (mark each error line // syntax-error or // semantic)')
    else:
        head.append('// status: well-formed (as far as the example says)')
    return '\n'.join(head) + '\n\n' + code.rstrip() + '\n'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--write', action='store_true')
    ap.add_argument('--list', nargs='?', const='')
    ap.add_argument('labels', nargs='*')
    a = ap.parse_args()
    if a.list is not None:
        total = 0
        for path in sorted(glob.glob(os.path.join(SPEC, a.list or '*', '*.md'))):
            if os.path.relpath(path, SPEC).startswith(('gram', 'lib')):
                continue
            n = len(examples(path))
            if n:
                total += n
                print(f'{n:4} {os.path.basename(path)[:-3]}')
        print(f'{total:4} code blocks in examples')
        return 0
    status = 0
    for label in a.labels:
        path = subclause_path(label)
        seen = {}
        for number, paragraph, code in examples(path):
            seen[number] = seen.get(number, 0) + 1
            part = seen[number]
            name = f'{number}.cpp'
            if part > 1:
                name = f'{number}.{part}.cpp'
            target = os.path.join(OUT, label, name)
            text = render(label, path, number, paragraph, code, part if part > 1 else 0)
            if os.path.exists(target):
                body = open(target).read().split('\n\n', 1)[-1]
                mine = text.split('\n\n', 1)[-1]
                strip = lambda t: [re.sub(r'\s*// (syntax-error|semantic)\s*$', '', l) for l in t.split('\n')]
                if strip(body) != strip(mine):
                    print(f'{os.path.relpath(target, R)}: differs from the standard\'s example')
                    status = 1
                continue
            if a.write:
                os.makedirs(os.path.dirname(target), exist_ok=True)
                with open(target, 'w') as f:
                    f.write(text)
            print(f'{os.path.relpath(target, R)}')
    return status


if __name__ == '__main__':
    sys.exit(main())
