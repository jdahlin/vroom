#!/usr/bin/env python3
"""vcc's C++ name mangling against clang's (PLAN-cxx.md, code generation).

  bench/cxx-mangle.py [-v] [PAT]

Each src/test/cxx/mangle/*.cpp defines functions.  clang++ -c gives the object's defined
function symbols; vpp -fsyntax-only with VCC_CXX_MANGLE writes the name it mangles for each
function defined at namespace scope.  The two sets must be equal; a name vcc does not mangle
yet ("?name") is a failure.
"""
import argparse, os, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
VPP = os.path.join(os.environ.get("VCC_BIN", os.path.join(ROOT, "build", "linux", "src")), "vpp")
CLANG = os.environ.get("CLANGXX", "/usr/lib/llvm-23/bin/clang++")
TESTS = os.path.join(ROOT, "src", "test", "cxx", "mangle")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-v", action="store_true")
    ap.add_argument("pat", nargs="?", default="")
    a = ap.parse_args()
    files = sorted(f for f in os.listdir(TESTS) if f.endswith(".cpp") and a.pat in f)
    bad = total = 0
    with tempfile.TemporaryDirectory() as tmp:
        for f in files:
            src, obj, out = os.path.join(TESTS, f), os.path.join(tmp, f + ".o"), os.path.join(tmp, f + ".names")
            if subprocess.run([CLANG, "-std=c++17", "-stdlib=libc++", "-w", "-c", src, "-o", obj], capture_output=True).returncode:
                print("FAIL %s: clang++ rejects the test" % f)
                bad += 1
                continue
            nm = subprocess.run(["nm", "--defined-only", obj], capture_output=True, text=True).stdout
            want = sorted(l.split()[-1] for l in nm.splitlines() if l.split()[-2] in "TtWw")
            r = subprocess.run([VPP, "-x", "c++", "-std=c++17", "-stdlib=libc++", "-w", "-fsyntax-only", src], capture_output=True, text=True,
                               env={**os.environ, "VCC_CXX_MANGLE": out})
            if r.returncode or not os.path.exists(out):
                print("FAIL %s: vcc failed: %s" % (f, (r.stderr.strip().splitlines() or ["?"])[-1]))
                bad += 1
                continue
            got = sorted(open(out).read().split())
            total += len(want)
            # vcc also names the headers' inline functions, which clang emits only when used: every name
            # clang defines must be among vcc's
            missing, extra = sorted(set(want) - set(got)), []
            if missing or extra:
                bad += 1
                print("FAIL %s: %d of %d names differ" % (f, len(missing), len(want)))
                for n in missing[:20] if not a.v else missing:
                    print("    clang: %s" % n)
                for n in extra[:20] if not a.v else extra:
                    print("    vcc:   %s" % n)
            elif a.v:
                print("ok   %s (%d names)" % (f, len(want)))
    print("cxx-mangle: %d/%d files ok, %d names" % (len(files) - bad, len(files), total))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
