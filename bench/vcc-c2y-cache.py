#!/usr/bin/env python3
"""C23/C2y extension diagnostics stay distinct across shared-header caches."""
import os
from pathlib import Path
import subprocess
import tempfile
from hostenv import BUILD

vpp = str(Path(os.environ.get('VCC_BIN', BUILD)) / 'vpp')
headers = {
    'complex': 'static inline void f(void) { _Complex double z=0; ++z; }\n',
    'range': 'static inline void f(void) { switch(1){case 1 ... 3:break;default:break;} }\n',
}
with tempfile.TemporaryDirectory(prefix='vcc-c2y-cache-') as tmp:
    directory = Path(tmp)
    for name, header in headers.items():
        (directory / 'shared.h').write_text('#ifndef SHARED_H\n#define SHARED_H\n' + header + '#endif\n')
        for std in ('c23', 'c2y'):
            (directory / (std + '.c')).write_text('#include "shared.h"\nint main(void){f();return 0;}\n')
        for target in ('x86_64-linux-gnu', 'aarch64-linux-gnu'):
            for first in ('c23', 'c2y'):
                order = [first] * 4 + ['c23', 'c2y'] * 4
                for replay in (False, True):
                    rows = []
                    for std in order:
                        rows.append('\x1f'.join(['-', '-fsyntax-only', '--target=' + target,
                                                 '-std=' + std, '-pedantic-errors', str(directory / (std + '.c'))]))
                    jobs = directory / 'jobs'
                    jobs.write_text('\n'.join(rows) + '\n')
                    env = dict(os.environ)
                    if replay:
                        env.update(VCC_DR='1', VCC_DR_GAIN='0')
                    else:
                        env.pop('VCC_DR', None)
                        env.pop('VCC_DRREC', None)
                    result = subprocess.run([vpp, '--batch', str(jobs), '-j', '1', '-C', tmp],
                                            env=env, capture_output=True, text=True)
                    output = result.stdout + result.stderr
                    expected = order.count('c23')
                    assert result.returncode == 1 and output.count('failed: ' + str(directory / 'c23.c')) == expected, output
                    assert 'failed: ' + str(directory / 'c2y.c') not in output, output
                    assert f'{len(order)-expected}/{len(order)} ok' in output, output
print('C23/C2y mixed-mode cache diagnostics: 16 batches passed')
