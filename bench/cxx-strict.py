#!/usr/bin/env python3
"""The json test suite built by vcc alone: what vcc does not compile yet, and the tests that pass (PLAN-cxx.md)

  bench/cxx-strict.py [-j 6] [-v] [--top N] [PAT]

Per nlohmann/json test TU: vpp -O0 -c with VCC_CXX_MISSING (each function whose body vcc did not build,
with the first thing it could not build), and clang++ -O0 -c once for reference (cached).  A function the
TU's own object should define (clang's defines it) that vcc's leaves undefined is a missing compile: the TU
fails with them listed.  A TU with none is linked with doctest's main (clang's unit.cpp, the harness) and
run.  At the end: the TUs complete and passing, and the missing compiles counted by vcc's reason (where in
codegen it gave up), the most common first.
"""
import argparse, collections, concurrent.futures as cf, importlib.util, os, re, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("cp", os.path.join(HERE, "cxx-profile.py"))
cp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cp)
VPP = os.path.join(os.environ.get("VCC_BIN", os.path.join(os.path.dirname(HERE), "build", "linux", "src")), "vpp")
NM = os.environ.get("NM", "llvm-nm-23")


def syms(obj, defined):
    r = subprocess.run([NM, "--defined-only" if defined else "--undefined-only", obj], capture_output=True, text=True)
    return set(l.split()[-1] for l in r.stdout.splitlines() if l.strip() and (not defined or re.search(r" [TtWwVv] ", l)))


def clang_obj(tu, outdir):
    name, src, fl = tu
    ref = os.path.join(outdir, name + ".clang.o")
    if not os.path.exists(ref) or os.path.getmtime(ref) < os.path.getmtime(src):
        r = subprocess.run([cp.CLANG, "-stdlib=libc++", "-O0", "-c", *fl, "-w", src, "-o", ref], capture_output=True, text=True)
        if r.returncode:
            return None
    return ref


def build(tu, outdir, main_obj, timeout):
    """(name, state, missing [(symbol, reason)], detail, vcc seconds)"""
    name, src, fl = tu
    obj, miss, exe = (os.path.join(outdir, name + s) for s in (".vcc.o", ".missing", ".exe"))
    ref = clang_obj(tu, outdir)
    if not ref:
        return name, "clang failed", [], [], 0
    t0 = time.time()
    r = subprocess.run([VPP, "-x", "c++", "-stdlib=libc++", *fl, "-w", "-c", src, "-o", obj], capture_output=True, text=True,
                       env=dict(os.environ, VCC_CXX_MISSING=miss))
    secs = time.time() - t0
    if r.returncode:
        return name, "not compiled", [], (r.stderr.strip().splitlines() or ["?"])[:1], secs
    why = {}
    if os.path.exists(miss):
        for l in open(miss, errors="replace"):
            s, _, w = l.rstrip("\n").partition("\t")
            why.setdefault(s, w)
    missing = sorted((s, why.get(s, "(no body seen: not instantiated or not declared with a body)")) for s in syms(obj, False) & syms(ref, True))
    if missing:
        return name, "missing", missing, [], secs
    r = subprocess.run([cp.CLANG, "-stdlib=libc++", main_obj, obj, "-o", exe], capture_output=True, text=True)
    if r.returncode:
        return name, "link", [], [l for l in r.stderr.splitlines() if "undefined" in l][:3] or r.stderr.splitlines()[-1:], secs
    try:
        r = subprocess.run([exe], capture_output=True, text=True, errors="replace", timeout=timeout, cwd=os.path.dirname(src))
    except subprocess.TimeoutExpired:
        return name, "timeout", [], [], secs
    if r.returncode:
        return name, "fail %d" % r.returncode, [], [l for l in (r.stdout + r.stderr).splitlines() if l.strip()][-2:], secs
    return name, "pass", [], [], secs


def where(reason):
    """a reason's place in codegen (file:line), its key for counting"""
    m = re.match(r"(\S+:\d+):", reason)
    return m.group(1) if m else reason[:60]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-j", type=int, default=6)
    ap.add_argument("-v", action="store_true", help="every missing function of every TU")
    ap.add_argument("--top", type=int, default=15)
    ap.add_argument("--timeout", type=int, default=300)
    ap.add_argument("pat", nargs="?", default="")
    a = ap.parse_args()
    tus = [t for t in cp.tus("json") if a.pat in t[0] and t[0] != "unit"]
    outdir = os.path.join(cp.outdir("json", "vcc"), "strict")
    os.makedirs(outdir, exist_ok=True)
    main_obj = os.path.join(outdir, "unit-main.o")
    if not os.path.exists(main_obj):
        subprocess.run([cp.CLANG, "-stdlib=libc++", "-O0", "-c", *tus[0][2], "-w", os.path.join(os.path.dirname(tus[0][1]), "unit.cpp"),
                        "-o", main_obj], check=True)
    counts, byplace, example, total_missing, secs = collections.Counter(), collections.Counter(), {}, 0, 0.0
    with cf.ThreadPoolExecutor(a.j) as ex:
        for name, state, missing, detail, s in ex.map(lambda t: build(t, outdir, main_obj, a.timeout), tus):
            counts[state.split()[0]] += 1
            secs += s
            total_missing += len(missing)
            for sym, why in missing:
                byplace[where(why)] += 1
                example.setdefault(where(why), (sym, why))
            if state != "pass" or a.v:
                print(f"{state:12} {name}" + (f": {len(missing)} functions not compiled" if missing else ""))
                for sym, why in missing if a.v else missing[:2]:
                    print(f"             {sym[:90]}  <- {why[:110]}")
                for d in detail:
                    print("             " + d[:160])
    print(f"cxx-strict: {len(tus)} TUs; pass {counts['pass']}, missing compiles {counts['missing']}, test failed {counts['fail']}, "
          f"timeout {counts['timeout']}, link failed {counts['link']}, not compiled {counts['not']}")
    print(f"  {total_missing} functions not compiled (summed over TUs); vcc {secs:.1f} s (wall per TU, summed)")
    if byplace:
        print(f"  where vcc gave up, the most common first:")
        for place, n in byplace.most_common(a.top):
            sym, why = example[place]
            print(f"  {n:6}  {place:28} e.g. {why[len(place) + 1:][:70].strip()}")
    return 0 if counts["pass"] == len(tus) else 1


if __name__ == "__main__":
    sys.exit(main())
