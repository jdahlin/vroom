#!/usr/bin/env python3
"""PLAN-cxx.md's proof of concept: how far vcc gets on nlohmann/json's tests.

  bench/cxx-json.py [-j 6] [--stage pp|syntax|obj] [PAT]

Per test TU, the stages in order, each stopping at its first failure:
  pp      vpp -E token stream equal to clang++ -E -P (vpp --cmp)
  syntax  vpp -fsyntax-only
  obj     vpp -c at -O2
(run and time come with later steps).  Prints the count per stage and the
most common first errors, so the next construct to implement is the top line.
Sources: work/cxx/json (see PLAN-cxx.md X0); flags as bench/cxx-profile.py.
"""
import argparse, collections, importlib.util, os, re, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("cxxprof", os.path.join(HERE, "cxx-profile.py"))
cp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cp)
BIN = os.environ.get("VCC_BIN") or os.path.join(cp.ROOT, "build", "linux", "src")
VPP = os.path.join(BIN, "vpp")
STAGES = ["pp", "syntax", "obj"]


def first_error(text):
    for l in text.splitlines():
        if "error:" in l:
            return re.sub(r"^.*?error:\s*", "", l).strip()
    return (text.strip().splitlines() or ["(no output)"])[0]


def bucket(msg):
    """The error without names and numbers, for the histogram."""
    return re.sub(r"'[^']*'", "'X'", re.sub(r"\d+", "N", msg))[:120]


def one(tu, last, outdir):
    name, src, fl = tu
    base = ["-x", "c++", "-stdlib=libc++", *fl, "-w"]
    ref = os.path.join(outdir, name + ".ii")
    if not os.path.exists(ref):
        r = subprocess.run([cp.CLANG, "-stdlib=libc++", "-E", "-P", *fl, "-w", src, "-o", ref], capture_output=True, text=True)
        if r.returncode:
            return name, "ref", "clang -E failed: " + first_error(r.stderr)
    for st in STAGES[:STAGES.index(last) + 1]:
        if st == "pp":
            a = [VPP, "--cmp", ref, *base, src]
        elif st == "syntax":
            a = [VPP, *base, "-fsyntax-only", src]
        else:
            a = [VPP, *base, "-O2", "-c", src, "-o", os.path.join(outdir, name + ".o")]
        try:
            r = subprocess.run(a, capture_output=True, text=True, timeout=300)
        except subprocess.TimeoutExpired:
            return name, st, "timeout"
        if r.returncode:
            return name, st, first_error(r.stderr + r.stdout)
    return name, "ok", ""


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-j", type=int, default=6)
    ap.add_argument("--stage", choices=STAGES, default="obj")
    ap.add_argument("pat", nargs="?", default="")
    a = ap.parse_args()
    tus = [t for t in cp.tus("json") if a.pat in t[0]]
    outdir = cp.outdir("json", "vcc")
    with ThreadPoolExecutor(a.j) as ex:
        res = list(ex.map(lambda t: one(t, a.stage, outdir), tus))
    at = collections.Counter(st for _, st, _ in res)
    print(f"json: {len(res)} TUs; " + ", ".join(f"{s} failed {at[s]}" for s in ["ref"] + STAGES if at[s])
          + f"; passed through {a.stage}: {at['ok']}")
    for st in ["ref"] + STAGES:
        errs = collections.Counter(bucket(m) for _, s, m in res if s == st)
        for m, n in errs.most_common(8):
            ex_ = next(nm for nm, s, mm in res if s == st and bucket(mm) == m)
            print(f"  {st:6} {n:3}x  {m}   (e.g. {ex_})")
    return 0 if at["ok"] == len(res) else 1


if __name__ == "__main__":
    sys.exit(main())
