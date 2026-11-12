#!/usr/bin/env python3
"""Check generated VIR programs and deterministic reproduction."""
import argparse
from pathlib import Path
import subprocess
import tempfile

import hostenv
from virgen import generate


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--bin-dir', type=Path, default=Path(hostenv.BUILD))
    args = ap.parse_args()
    exe = (args.bin_dir / 'virtest').resolve()
    cases = [(seed, seed % 96) for seed in range(100)] + [(42, 4096)]
    with tempfile.TemporaryDirectory(prefix='virgen-test-') as tmp:
        path = Path(tmp) / 'unit.vir'
        for seed, size in cases:
            source = generate(seed, size)
            if source != generate(seed, size):
                raise SystemExit(f'seed {seed}: nondeterministic output')
            path.write_text(source)
            p = subprocess.run([str(exe), str(path)], capture_output=True, text=True, timeout=30)
            if p.returncode:
                raise SystemExit(f'seed {seed}, size {size}: exit {p.returncode}\n{p.stdout}{p.stderr}')
    print(f'virgen-test: {len(cases)}/{len(cases)} passed')


if __name__ == '__main__':
    main()
