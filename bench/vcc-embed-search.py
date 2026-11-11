#!/usr/bin/env python3
"""Clang oracle for C23 embed search order, queries, and dependencies."""
import os
from pathlib import Path
import subprocess
import tempfile
from hostenv import BUILD, llvm

vpp = str(Path(os.environ.get('VCC_BIN', BUILD)) / 'vpp')
clang = llvm('clang')
checks = 0
with tempfile.TemporaryDirectory(prefix='vcc-embed-search-') as tmp:
    root = Path(tmp)
    for directory in ('sub/nested', 'first', 'second', 'include-only'):
        (root / directory).mkdir(parents=True, exist_ok=True)
    for name, value in {
        'sub/same.bin': 11, 'same.bin': 22, 'first/same.bin': 33,
        'second/same.bin': 44, 'fallback.bin': 22, 'first/fallback.bin': 33,
        'first/ordered.bin': 33, 'second/ordered.bin': 44,
        'second/last.bin': 44, 'root-only.bin': 55,
        'include-only/include.bin': 66, 'sub/nested/local.bin': 77,
        'sub/local.bin': 88, 'local.bin': 99,
    }.items():
        (root / name).write_bytes(bytes([value]))
    (root / 'empty.bin').write_bytes(b'')
    (root / 'sub/nested/embedding.h').write_text('#embed "local.bin"\n')
    cases = [
        ('includer', '#embed "same.bin"\n', '11'),
        ('cwd', '#embed "fallback.bin"\n', '22'),
        ('first-dir', '#embed "ordered.bin"\n', '33'),
        ('last-dir', '#embed "last.bin"\n', '44'),
        ('angle-dir', '#embed <same.bin>\n', '33'),
        ('self', '#embed __FILE__ limit(1)\n', '35'),
        ('nested', '#include "nested/embedding.h"\n', '77'),
        ('absolute', '#embed "' + str(root / 'root-only.bin') + '"\n', '55'),
        ('queries', ''.join(f'#if __has_embed({arg}) == {value}\n{value}\n#else\n#error wrong embed query\n#endif\n'
                            for arg, value in [('"fallback.bin"', 1), ('__FILE__', 1), ('"empty.bin"', 2),
                                               ('<root-only.bin>', 0), ('"include.bin"', 0), ('<include.bin>', 0)]),
         '1 1 2 0 0 0'),
        ('angle-cwd-reject', '#embed <root-only.bin>\n', None),
        ('include-path-reject', '#embed "include.bin"\n', None),
    ]
    for target in ('x86_64-linux-gnu', 'aarch64-linux-gnu'):
        flags = ['--target=' + target, '-std=c23', '--embed-dir=first',
                 '--embed-dir=second', '-Iinclude-only']
        for name, source, expected in cases:
            path = 'sub/' + name + '.c'
            (root / path).write_text(source)
            modes = [['-E', '-P']] + ([['-M', '-MT', 'probe.o']] if expected is not None else [])
            for mode in modes:
                outputs = []
                for compiler in (clang, vpp):
                    result = subprocess.run([compiler, *flags, *mode, path], cwd=root,
                                            capture_output=True, text=True, timeout=10)
                    if expected is None:
                        assert result.returncode != 0 and 'file not found' in result.stderr, (compiler, name, result)
                    else:
                        assert result.returncode == 0, (compiler, target, name, result.stderr)
                        outputs.append(result.stdout.replace('\\\n', ' ').split())
                if expected is not None:
                    assert outputs[0] == outputs[1], (target, name, mode, outputs)
                    if mode[0] == '-E':
                        assert outputs[0] == expected.split(), (target, name, outputs)
                    elif name == 'self':
                        assert outputs[0].count(path) == 1, outputs
                checks += 1
print(f'embed search: {checks} Clang/vcc comparisons passed (both Linux targets)')
