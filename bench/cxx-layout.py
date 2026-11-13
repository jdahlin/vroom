#!/usr/bin/env python3
"""The C++ front end's record layouts, checked by clang (PLAN-cxx.md X1).

  bench/cxx-layout.py [-j 6] [PAT]

For each json test TU: vpp -fsyntax-only with VCC_CXX_LAYOUT writes a
static_assert(sizeof(X) == N && alignof(X) == A) for every class it laid out
under a name clang can resolve; clang++ then compiles the TU with them
appended (-fno-access-control: private nested classes too).  Any failing
assertion is a layout vcc got wrong.
"""
import argparse, importlib.util, os, re, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("cxxjson", os.path.join(HERE, "cxx-json.py"))
cj = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cj)
cp = cj.cp


def one(tu, outdir):
    name, src, fl = tu
    asserts = os.path.join(outdir, name + ".layout")
    r = subprocess.run([cj.VPP, "-x", "c++", "-stdlib=libc++", *fl, "-w", "-fsyntax-only", src],
                       capture_output=True, text=True, errors="replace", env={**os.environ, "VCC_CXX_LAYOUT": asserts})
    if r.returncode or not os.path.exists(asserts):
        return name, 0, ["vcc failed: " + cj.first_error(r.stderr)]
    n = sum(1 for _ in open(asserts))
    wrap = os.path.join(outdir, name + ".layout.cpp")
    with open(wrap, "w") as f:
        f.write(f'#include "{src}"\n#include "{asserts}"\n')
    r = subprocess.run([cp.CLANG, "-stdlib=libc++", *fl, "-w", "-fsyntax-only", "-fno-access-control", "-ferror-limit=0", wrap],
                       capture_output=True, text=True, errors="replace")
    bad = [l for l in r.stderr.splitlines() if "error:" in l]
    return name, n, bad


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-j", type=int, default=6)
    ap.add_argument("pat", nargs="?", default="")
    a = ap.parse_args()
    tus = [t for t in cp.tus("json") if a.pat in t[0]]
    outdir = cp.outdir("json", "layout")
    with ThreadPoolExecutor(a.j) as ex:
        res = list(ex.map(lambda t: one(t, outdir), tus))
    total = sum(n for _, n, _ in res)
    bad = [(nm, b) for nm, _, bl in res for b in bl]
    print(f"cxx-layout: {len(res)} TUs, {total} layouts asserted, {len(bad)} errors")
    seen = set()
    for nm, b in bad:
        k = re.sub(r"^.*?error: ", "", b)
        if k not in seen:
            seen.add(k)
            print(f"  {nm}: {k[:200]}")
        if len(seen) >= 12:
            break
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
