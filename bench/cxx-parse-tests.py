#!/usr/bin/env python3
"""The C++ parser's own cases (src/test/cxx/parse): ok-*.cpp must parse, bad-*.cpp must not.
<1 s.  bench/cxx-parse-tests.py [--bin-dir DIR]"""
import glob, os, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
args = sys.argv[1:]
BIN = args[args.index("--bin-dir") + 1] if "--bin-dir" in args else os.environ.get("VCC_BIN") or os.path.join(ROOT, "build", "linux", "src")
VPP = os.path.join(BIN, "vpp")
fails = 0
cases = sorted(glob.glob(os.path.join(ROOT, "src/test/cxx/parse/*.cpp")))
for f in cases:
    want_ok = os.path.basename(f).startswith("ok-")
    r = subprocess.run([VPP, "-x", "c++", "-std=c++20", "-stdlib=libc++", "-fsyntax-only", "-w", f], capture_output=True, text=True)
    if (r.returncode == 0) != want_ok:
        fails += 1
        print(f"FAIL {os.path.basename(f)}: " + (r.stderr.strip().splitlines() or ["accepted"])[0])
print(f"cxx-parse: {len(cases) - fails}/{len(cases)} ok")
sys.exit(1 if fails else 0)
