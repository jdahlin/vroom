#!/usr/bin/env python3
"""All kernel C TUs compiled (-c) in one vpp process, as vccd does: wall, CPU, phases.

  vcc-cbatch.py [PAT] [-j N]    objects go to $OBJ/cb (default $TMPDIR/vcc-obj)
"""
import os, subprocess, sys
import importlib.util
spec = importlib.util.spec_from_file_location('vo', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'vcc-obj.py'))
vo = importlib.util.module_from_spec(spec); spec.loader.exec_module(vo)
av = sys.argv[1:]; j = 6
if '-j' in av: k = av.index('-j'); j = int(av[k+1]); del av[k:k+2]
D = f'{vo.OBJ}/cb'; os.makedirs(D, exist_ok=True)
jf = f'{D}/jobs'
with open(jf, 'w') as f:
    for obj, a in vo.c_tus(av[0] if av else None):
        f.write('\x1f'.join(['-'] + a + ['-c', '-o', f'{D}/{obj.replace("/", "__")}']) + '\n')
sys.exit(subprocess.run([vo.VPP, '--batch', jf, '-j', str(j), '-C', vo.B]).returncode)
