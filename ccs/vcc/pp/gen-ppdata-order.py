#!/usr/bin/env python3
"""gen-ppdata-order.py CLANG 'FLAGS' 'FLAGS' ... : the names clang predefines,
in its -dD order, merged over the flag sets (a name one set lacks goes after
its predecessor in the set that has it).  For -dD (gen-ppdata.sh)."""
import subprocess, sys
cl, sets = sys.argv[1], sys.argv[2:]
order = []
for fl in sets:
    out = subprocess.run([cl] + fl.split() + ['-dD', '-E', '-P', '-x', 'c', '/dev/null'],
                         capture_output=True, text=True, check=True).stdout
    prev = None
    for l in out.splitlines():
        if not l.startswith('#define '): continue
        n = l.split()[1].split('(')[0]
        if n not in order:
            order.insert(order.index(prev) + 1 if prev in order else len(order), n)
        prev = n
for n in order: print(f'\t"{n}",')
