#!/usr/bin/env python3
"""The json test suite as a few unity TUs: headers and template instances compiled once per group (PLAN-cxx.md)

  bench/cxx-unity.py [-j 6] [-v] [--regroup]

What shared headers buy, measured with what exists today.  The 70 test TUs are put in groups, each one
file that #includes its members (greedy: a TU joins the first group clang still accepts it in; the
grouping is cached in work/.../unity/groups.txt).  Then as cxx-weakfill.py does per TU: vpp -O0 -c per
group (timed), clang -O0 -c of the group made weak, linked with doctest's main, run.  Prints the sum of
vcc's CPU time against clang -O2's per-TU 243 s.
"""
import argparse, concurrent.futures as cf, importlib.util, os, re, resource, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("cp", os.path.join(HERE, "cxx-profile.py"))
cp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cp)
spec = importlib.util.spec_from_file_location("wf", os.path.join(HERE, "cxx-weakfill.py"))
wf = importlib.util.module_from_spec(spec)
spec.loader.exec_module(wf)
CLANG_O2_CPU = 243.0


def unity_text(members):
    """one file including its members; each member's macros are undefined after it where doctest's are not"""
    out = []
    for m in members:
        out.append(f'#include "{m[1]}"\n')
    return "".join(out)


def accepts(path, fl):
    r = subprocess.run([cp.CLANG, "-stdlib=libc++", "-fsyntax-only", "-w", *fl, path], capture_output=True)
    return r.returncode == 0


def config_key(src):
    """the macros a test defines that configure json.hpp or doctest (JSON_*, DOCTEST_*): only TUs with the same
    ones may share a group, which includes the headers once"""
    return tuple(sorted(set(re.findall(r"^\s*#\s*define\s+((?:JSON|DOCTEST)_\w+.*)$", open(src).read(), re.M))))


def clang_runs(members, outdir, name, flags, main_obj, timeout):
    """does clang's own -O0 build of the group pass its tests (else grouping changed what they test)"""
    src, obj, exe = (os.path.join(outdir, name + s) for s in (".check.cpp", ".check.o", ".check.exe"))
    with open(src, "w") as f:
        f.write(unity_text(members))
    if subprocess.run([cp.CLANG, "-stdlib=libc++", "-O0", "-c", "-w", *flags, src, "-o", obj], capture_output=True).returncode:
        return False
    if subprocess.run([cp.CLANG, "-stdlib=libc++", main_obj, obj, "-o", exe], capture_output=True).returncode:
        return False
    try:
        return subprocess.run([exe], capture_output=True, timeout=timeout, cwd=os.path.dirname(members[0][1])).returncode == 0
    except subprocess.TimeoutExpired:
        return False


def make_groups(tus, outdir, jobs):
    """greedy: each TU into the first group of its configuration clang accepts it in"""
    groups = []
    for t in tus:
        placed = False
        cands = [i for i in range(len(groups)) if config_key(groups[i][0][1]) == config_key(t[1])]

        def tryg(i):
            p = os.path.join(outdir, f"try{i}.cpp")
            with open(p, "w") as f:
                f.write(unity_text(groups[i] + [t]))
            return i, accepts(p, t[2])
        with cf.ThreadPoolExecutor(jobs) as ex:
            for i, ok in ex.map(tryg, cands):
                if ok and not placed:
                    groups[i].append(t)
                    placed = True
        if not placed:
            groups.append([t])
        print(f"  {t[0]} -> group {next(i for i, g in enumerate(groups) if t in g)}", flush=True)
    return groups


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-j", type=int, default=6)
    ap.add_argument("-v", action="store_true")
    ap.add_argument("--regroup", action="store_true")
    ap.add_argument("--timeout", type=int, default=600)
    a = ap.parse_args()
    tus = [t for t in cp.tus("json") if t[0] != "unit"]
    outdir = os.path.join(cp.outdir("json", "vcc"), "unity")
    os.makedirs(outdir, exist_ok=True)
    gfile = os.path.join(outdir, "groups.txt")
    byname = {t[0]: t for t in tus}
    if a.regroup or not os.path.exists(gfile):
        # (flags differ for two TUs: those stay alone)
        same = [t for t in tus if t[2] == tus[0][2]]
        groups = make_groups(same, outdir, a.j) + [[t] for t in tus if t[2] != tus[0][2]]
        # (a group whose clang build fails its tests where its members alone pass: split)
        main_obj0 = os.path.join(outdir, "unit-main.o")
        if not os.path.exists(main_obj0):
            subprocess.run([cp.CLANG, "-stdlib=libc++", "-O0", "-c", *tus[0][2], "-w", os.path.join(os.path.dirname(tus[0][1]), "unit.cpp"),
                            "-o", main_obj0], check=True)
        with cf.ThreadPoolExecutor(a.j) as ex:
            ok = list(ex.map(lambda ig: len(ig[1]) == 1 or clang_runs(ig[1], outdir, f"check{ig[0]}", ig[1][0][2], main_obj0, a.timeout),
                             list(enumerate(groups))))
        groups = [g for g, k in zip(groups, ok) if k] + [[t] for g, k in zip(groups, ok) if not k for t in g]
        with open(gfile, "w") as f:
            for g in groups:
                f.write(" ".join(t[0] for t in g) + "\n")
    groups = [[byname[n] for n in l.split()] for l in open(gfile) if l.strip()]
    units = []
    for i, g in enumerate(groups):
        src = os.path.join(outdir, f"group{i}.cpp")
        text = unity_text(g)
        if not os.path.exists(src) or open(src).read() != text:
            with open(src, "w") as f:
                f.write(text)
        units.append((f"group{i}", src, g[0][2]))
    print(f"{len(tus)} TUs in {len(groups)} groups: " + ", ".join(str(len(g)) for g in groups))
    main_obj = os.path.join(outdir, "unit-main.o")
    if not os.path.exists(main_obj):
        subprocess.run([cp.CLANG, "-stdlib=libc++", "-O0", "-c", *tus[0][2], "-w", os.path.join(os.path.dirname(tus[0][1]), "unit.cpp"),
                        "-o", main_obj], check=True)
    with cf.ThreadPoolExecutor(a.j) as ex:
        bad = {n: w for n, w in ex.map(lambda t: wf.prepare(t, outdir), units) if w}
    r0 = resource.getrusage(resource.RUSAGE_CHILDREN)
    w0 = time.time()
    with cf.ThreadPoolExecutor(a.j) as ex:
        bad.update({n: w for n, w in ex.map(lambda t: wf.vcc(t, outdir), units) if w})
    r1 = resource.getrusage(resource.RUSAGE_CHILDREN)
    cpu, wall = (r1.ru_utime + r1.ru_stime) - (r0.ru_utime + r0.ru_stime), time.time() - w0
    passed = 0
    with cf.ThreadPoolExecutor(a.j) as ex:
        for name, what, detail, share in ex.map(lambda t: wf.link_run(t, outdir, main_obj, a.timeout), [u for u in units if u[0] not in bad]):
            k = len(groups[int(name[5:])])
            passed += k if what in ("ok", "ok-unres") else 0
            print(f"{what:8} {name} ({k} TUs)  vcc has {share * 100:.0f} % of clang's functions")
            for d in detail[:3] if (what not in ("ok", "ok-unres") or a.v) else []:
                print("         " + d[:160])
    for n, w in sorted(bad.items()):
        print(f"FAIL     {n}: {w}")
    print(f"cxx-unity: {len(tus)} TUs in {len(groups)} groups; the TUs of passing groups: {passed}")
    print(f"  vcc -O0: {cpu:.1f} s CPU ({wall:.1f} s wall, -j {a.j}); clang -O2 per TU {CLANG_O2_CPU:.0f} s: {CLANG_O2_CPU / max(cpu, 1e-9):.1f}x")
    return 0


if __name__ == "__main__":
    sys.exit(main())
