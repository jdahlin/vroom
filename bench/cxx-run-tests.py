#!/usr/bin/env python3
"""C++ programs compiled by vcc, linked by clang++ and run (PLAN-cxx.md, code generation).

  bench/cxx-run-tests.py [-v] [PAT]

Each src/test/cxx/run/*.cpp: `vpp -x c++ -c` (every function definition must get code:
"function definitions N, with code N"), clang++ links the object, the program must exit 0, and
the object's defined symbols must be the ones clang++ -c defines for the same file (lambdas'
functions aside: their names are each compiler's own).
"""
import argparse, os, re, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
VPP = os.path.join(os.environ.get("VCC_BIN", os.path.join(ROOT, "build", "linux", "src")), "vpp")
CLANG = os.environ.get("CLANGXX", "/usr/lib/llvm-23/bin/clang++")
TESTS = os.path.join(ROOT, "src", "test", "cxx", "run")


def symbols(obj):
    r = subprocess.run(["nm", "--defined-only", obj], capture_output=True, text=True)
    # (a lambda's functions have names of each compiler's own: $_N in clang's, __vcc_lambda_N here; __vcc_try_N: a try block's trampoline)
    return sorted(l.split()[-1] for l in r.stdout.splitlines()
                  if (" T " in l or " t " in l) and "$_" not in l and "__vcc_" not in l)


def one(path, tmp, verbose):
    name = os.path.basename(path)[:-4]
    obj, ref, exe = (os.path.join(tmp, name + s) for s in (".o", ".ref.o", ""))
    r = subprocess.run([VPP, "-x", "c++", "-std=c++17", "-c", path, "-o", obj], capture_output=True, text=True,
                       env={**os.environ, "VCC_CXX_PROGRESS": "1"})
    if r.returncode:
        return "vcc failed: " + (r.stderr.strip().splitlines() or ["?"])[-1]
    m = re.search(r"function definitions (\d+), with code (\d+)", r.stderr)
    if not m or m.group(1) != m.group(2):
        return "not every function got code: " + (m.group(0) if m else "no count")
    if subprocess.run([CLANG, "-std=c++17", "-c", path, "-o", ref], capture_output=True).returncode:
        return "clang++ rejects the test"
    # the test itself must be right: clang's build of it exits 0
    if subprocess.run([CLANG, ref, "-o", exe + ".ref"], capture_output=True).returncode or subprocess.run([exe + ".ref"]).returncode:
        return "the test fails when clang++ builds it"
    if symbols(obj) != symbols(ref):
        return "symbols differ: vcc %s, clang %s" % (symbols(obj), symbols(ref))
    r = subprocess.run([CLANG, obj, "-o", exe], capture_output=True, text=True)
    if r.returncode:
        return "link failed: " + (r.stderr.strip().splitlines() or ["?"])[0]
    r = subprocess.run([exe], capture_output=True)
    if r.returncode:
        return "exit %d" % r.returncode
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-v", action="store_true")
    ap.add_argument("pat", nargs="?", default="")
    a = ap.parse_args()
    files = sorted(os.path.join(TESTS, f) for f in os.listdir(TESTS) if f.endswith(".cpp") and a.pat in f)
    bad = 0
    with tempfile.TemporaryDirectory() as tmp:
        for f in files:
            why = one(f, tmp, a.v)
            if why:
                bad += 1
                print("FAIL %s: %s" % (os.path.basename(f), why))
            elif a.v:
                print("ok   %s" % os.path.basename(f))
    print("cxx-run: %d/%d ok" % (len(files) - bad, len(files)))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
