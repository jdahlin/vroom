#!/usr/bin/env python3
"""How much of what a json test needs does vcc's C++ code generation produce?  (PLAN-cxx.md)

  bench/cxx-coverage.py [-j 6] [-v] [PAT]

For each nlohmann/json test TU: clang++ -O0 -c and vpp -c; the functions clang defines (what the
test needs: everything it uses, templates' instances included) against the ones vcc defines, by
demangled name with libc++'s [abi:...] tags removed.  "needed N, have M" is the measure of how far
a test is from linking; vcc's other functions are inline functions of headers nothing uses.
With -v the needed functions vcc lacks are listed (work/cxx/out/json/coverage/TU.missing).
"""
import argparse, concurrent.futures as cf, importlib.util, os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("cp", os.path.join(HERE, "cxx-profile.py"))
cp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cp)
VPP = os.path.join(os.environ.get("VCC_BIN", os.path.join(os.path.dirname(HERE), "build", "linux", "src")), "vpp")
FILT = os.environ.get("CXXFILT", "llvm-cxxfilt-23")


def functions(obj):
    r = subprocess.run(["nm", "--defined-only", obj], capture_output=True, text=True)
    names = "\n".join(l.split()[-1] for l in r.stdout.splitlines() if re.search(r" [TtWw] ", l))
    d = subprocess.run([FILT], input=names, capture_output=True, text=True).stdout
    return set(re.sub(r"\[abi:[a-z0-9]+\]", "", l) for l in d.splitlines() if l)


def one(tu, outdir):
    name, src, fl = tu
    ref, obj = os.path.join(outdir, name + ".clang.o"), os.path.join(outdir, name + ".vcc.o")
    if not os.path.exists(ref) or os.path.getmtime(ref) < os.path.getmtime(src):
        r = subprocess.run([cp.CLANG, "-stdlib=libc++", "-O0", "-c", *fl, "-w", src, "-o", ref], capture_output=True, text=True)
        if r.returncode:
            return name, None, "clang failed"
    r = subprocess.run([VPP, "-x", "c++", "-stdlib=libc++", *fl, "-w", "-c", src, "-o", obj], capture_output=True, text=True)
    if r.returncode:
        return name, None, "vcc failed: " + (r.stderr.strip().splitlines() or ["?"])[0]
    need, have = functions(ref), functions(obj)
    with open(os.path.join(outdir, name + ".missing"), "w") as f:
        f.write("\n".join(sorted(need - have)) + "\n")
    return name, (len(need), len(need & have), len(have - need)), None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-j", type=int, default=6)
    ap.add_argument("-v", action="store_true")
    ap.add_argument("pat", nargs="?", default="")
    a = ap.parse_args()
    tus = [t for t in cp.tus("json") if a.pat in t[0]]
    outdir = os.path.join(cp.outdir("json", "vcc"), "coverage")
    os.makedirs(outdir, exist_ok=True)
    need = have = other = bad = 0
    with cf.ThreadPoolExecutor(a.j) as ex:
        for name, counts, why in ex.map(lambda t: one(t, outdir), tus):
            if why:
                bad += 1
                print(f"FAIL {name}: {why}")
                continue
            need, have, other = need + counts[0], have + counts[1], other + counts[2]
            if a.v:
                print(f"{name}: needed {counts[0]}, have {counts[1]}, others {counts[2]}")
    pct = 100.0 * have / need if need else 0.0
    print(f"cxx-coverage: {len(tus)} TUs, failed {bad}; functions needed {need}, have {have} ({pct:.1f} %), others {other}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
