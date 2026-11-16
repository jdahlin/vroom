#!/usr/bin/env python3
"""The C++ front end written against the standard uses no conditional
operator (a ? b : c, GNU's a ?: b): if/else, a switch, a table or a named
helper says the same more plainly (doc/PLAN-cxx-spec.md, Conventions).

  src/tools/check-no-conditional-operator.py FILE...

Prints each ? in code (not in comments, string or character literals) as
file:line: text and exits 1 if there is one.  The pre-commit hook
no-conditional-operator runs it on the front end's files.
"""
import re
import sys


def code_of(text):
    """the text with comments and literals blanked, lines kept"""
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith('//', i):
            j = text.find('\n', i)
            j = n if j < 0 else j
            out.append(' ' * (j - i))
            i = j
        elif text.startswith('/*', i):
            j = text.find('*/', i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r'[^\n]', ' ', text[i:j]))
            i = j
        elif c in '"\'':
            # a raw string literal R"delim( ... )delim"
            raw = re.match(r'R"([^ ()\\]{0,16})\(', text[i - 1:i + 18]) if i and text[i - 1] == 'R' else None
            if raw:
                end = text.find(')' + raw.group(1) + '"', i)
                j = n if end < 0 else end + len(raw.group(1)) + 1
            else:
                j = i + 1
                while j < n and text[j] != c and text[j] != '\n':
                    j += 2 if text[j] == '\\' else 1
            out.append(c + re.sub(r'[^\n]', ' ', text[i + 1:j]) + c)
            i = j + 1
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def main():
    found = 0
    for path in sys.argv[1:]:
        code = code_of(open(path, encoding='utf-8', errors='replace').read())
        for number, line in enumerate(code.split('\n'), 1):
            if '?' in line:
                found += 1
                print(f'{path}:{number}: conditional operator: {line.strip()[:100]}')
    return 1 if found else 0


if __name__ == '__main__':
    sys.exit(main())
