#!/usr/bin/env python3
"""The Itanium C++ ABI as the C++ front end cites it (doc/PLAN-cxx-sema.md,
section 2): `[abi:LABEL]`, one label per section.

The ABI's text has no licence that lets it be copied into the tree, so it
is fetched (bench/fetch-testsuites.sh itanium-abi: abi.html and
abi-eh.html at a pinned commit) and only an index is committed:
docs/itanium-abi-index.tsv, a line per section

    LABEL <tab> FILE <tab> NUMBER <tab> TITLE

which bench/cxx-spec-refs.py checks citations against.  A section of
abi.html is labelled by its HTML anchor (`vtable-components` is 2.5.2); one
of abi-eh.html, which has no anchors, by `eh-` and its number, or its title
when it has none (`eh-1.6.1`, `eh-unwind-raiseexception`).

  bench/itanium-abi.py           rewrite the index, and write each section's
                                 text to work/itanium-abi/LABEL.txt to read
  bench/itanium-abi.py --check   the index is what the fetched files give
"""
import html
import os
import re
import sys

R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE = os.path.join(R, 'work', 'testsuites', 'itanium-abi')
INDEX = os.path.join(R, 'docs', 'itanium-abi-index.tsv')
TEXT = os.path.join(R, 'work', 'itanium-abi')
HEADING = re.compile(r'<h([1-5])>(.*?)</h\1>', re.S | re.I)


def text_of(fragment):
    fragment = re.sub(r'<(br|p|li|tr|pre|h\d)\b[^>]*>', '\n', fragment, flags=re.I)
    fragment = re.sub(r'<[^>]+>', '', fragment)
    lines = [' '.join(html.unescape(line).split()) for line in fragment.split('\n')]
    return re.sub(r'\n{3,}', '\n\n', '\n'.join(lines)).strip()


def slug(title):
    return re.sub(r'[^a-z0-9]+', '-', title.lower()).strip('-')


def sections():
    """(label, file, number, title, text) per section, in order"""
    found = []
    for name in ('abi.html', 'abi-eh.html'):
        page = open(os.path.join(SOURCE, name), encoding='utf-8', errors='replace').read()
        heads = list(HEADING.finditer(page))
        for k, m in enumerate(heads):
            inner = m.group(2)
            title = text_of(inner).split("\n")[0].strip()
            anchor = re.search(r'href="?#([\w.-]+)', inner)
            number = re.match(r'^((?:\d+\.)*\d+)\.?\s+(.*)$', title)
            if number:
                number, title = number.group(1), number.group(2)
            else:
                number = ''
            if name == 'abi.html':
                if not anchor:
                    continue
                label = anchor.group(1).lower()
            else:
                label = 'eh-' + (number or slug(title))
            end = heads[k + 1].start() if k + 1 < len(heads) else len(page)
            found.append((label, name, number, title, text_of(page[m.end():end])))
    return found


def index_text(found):
    out = ['# The Itanium C++ ABI\'s sections: bench/itanium-abi.py from the files',
           '# bench/fetch-testsuites.sh itanium-abi fetches.  LABEL FILE NUMBER TITLE']
    seen = set()
    for label, name, number, title, _ in found:
        if label in seen:
            continue
        seen.add(label)
        out.append(f'{label}\t{name}\t{number}\t{title}')
    return '\n'.join(out) + '\n'


def main():
    if not os.path.isdir(SOURCE):
        sys.exit('itanium-abi: no work/testsuites/itanium-abi; bench/fetch-testsuites.sh itanium-abi')
    found = sections()
    text = index_text(found)
    if sys.argv[1:] == ['--check']:
        if not os.path.exists(INDEX) or open(INDEX, encoding='utf-8').read() != text:
            print('itanium-abi: docs/itanium-abi-index.tsv is not what the fetched files give')
            return 1
        print(f'itanium-abi: {len(text.splitlines()) - 2} sections, index current')
        return 0
    open(INDEX, 'w', encoding='utf-8').write(text)
    os.makedirs(TEXT, exist_ok=True)
    for label, name, number, title, body in found:
        with open(os.path.join(TEXT, label + '.txt'), 'w', encoding='utf-8') as f:
            f.write(f'{number} {title} [abi:{label}] ({name})\n\n{body}\n')
    print(f'itanium-abi: {len(text.splitlines()) - 2} sections -> {os.path.relpath(INDEX, R)}, '
          f'text in {os.path.relpath(TEXT, R)}/')
    return 0


if __name__ == '__main__':
    sys.exit(main())
