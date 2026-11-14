#!/usr/bin/env python3
"""The json test suite with its headers parsed once: a fork server in place of a precompiled header (PLAN-cxx.md)

  bench/cxx-fork.py [-j 6] [-v] [PAT]

Each TU is compiled on its own (its own object, its own tests), but the headers every test includes first
(doctest_compatibility.h, nlohmann/json.hpp) are parsed once per configuration: vpp -c of a prefix file
(the configuration's #defines, those #includes, #pragma vcc_fork) parses them, then forks one child per TU,
which continues from there with the TU's own text (translation-unit.c: cxx_fork_tus).  The TUs whose
#define JSON_*/DOCTEST_* lines or flags differ get prefixes of their own.  Then as cxx-weakfill.py: clang's
-O0 object made weak fills what vcc has no code for, linked with doctest's main, the test run.  vcc's CPU
time (the servers and their children) against clang -O2's 243 s.
"""
import argparse, concurrent.futures as cf, importlib.util, os, re, resource, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))


def load(name, file):
    spec = importlib.util.spec_from_file_location(name, os.path.join(HERE, file))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


cp = load("cp", "cxx-profile.py")
wf = load("wf", "cxx-weakfill.py")
CLANG_O2_CPU = 243.0
PREFIX_HEADERS = ['#include "doctest_compatibility.h"\n', "#include <nlohmann/json.hpp>\n"]


def config_lines(src):
    """the #define lines that configure json.hpp or doctest, before the TU's first #include"""
    out = []
    for line in open(src):
        if re.match(r"\s*#\s*include", line):
            break
        if re.match(r"\s*#\s*define\s+(JSON|DOCTEST)_", line):
            out.append(line.strip() + "\n")
    return tuple(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-j", type=int, default=6)
    ap.add_argument("-v", action="store_true")
    ap.add_argument("--timeout", type=int, default=300)
    ap.add_argument("pat", nargs="?", default="")
    a = ap.parse_args()
    tus = [t for t in cp.tus("json") if a.pat in t[0] and t[0] != "unit"]
    outdir = os.path.join(cp.outdir("json", "vcc"), "fork")
    os.makedirs(outdir, exist_ok=True)
    groups = {}
    for t in tus:
        groups.setdefault((config_lines(t[1]), tuple(t[2])), []).append(t)
    servers = []
    for i, ((cfg, flags), members) in enumerate(sorted(groups.items(), key=lambda kv: -len(kv[1]))):
        d = os.path.join(outdir, f"prefix{i}")
        os.makedirs(d, exist_ok=True)
        prefix = os.path.join(d, "prefix.cpp")
        with open(prefix, "w") as f:
            f.write("".join(cfg) + "".join(PREFIX_HEADERS) + "#pragma vcc_fork\nstruct __vcc_fork_barrier;\n#include \"__vcc_fork_tu.cpp\"\n")
        lst = os.path.join(d, "list.txt")
        with open(lst, "w") as f:
            for t in members:
                f.write(f"{t[1]} {os.path.join(outdir, t[0] + '.vcc.o')}\n")
        servers.append((d, prefix, lst, list(flags), members))
    print(f"{len(tus)} TUs, {len(servers)} prefixes: " + ", ".join(str(len(s[4])) for s in servers))
    main_obj = os.path.join(outdir, "unit-main.o")
    if not os.path.exists(main_obj):
        subprocess.run([cp.CLANG, "-stdlib=libc++", "-O0", "-c", *tus[0][2], "-w", os.path.join(os.path.dirname(tus[0][1]), "unit.cpp"),
                        "-o", main_obj], check=True)
    with cf.ThreadPoolExecutor(a.j) as ex:
        bad = {n: w for n, w in ex.map(lambda t: wf.prepare(t, outdir), tus) if w}
    for t in tus:	# (a stale object must not pass for one the server made)
        try:
            os.unlink(os.path.join(outdir, t[0] + ".vcc.o"))
        except FileNotFoundError:
            pass

    def serve(s):
        d, prefix, lst, flags, members = s
        # (the stub is written after the fork: no include looked up ahead, no directory listing cached)
        env = dict(os.environ, VCC_FORK_LIST=lst, VCC_FORK_DIR=d, VPP_NOPREFETCH="1", VPP_NODIRCACHE="1")
        r = subprocess.run([wf.VPP, "-x", "c++", "-stdlib=libc++", *flags, "-w", "-c", prefix, "-o", os.path.join(d, "prefix.o")],
                           capture_output=True, text=True, env=env)
        return d, r.returncode, r.stderr
    r0 = resource.getrusage(resource.RUSAGE_CHILDREN)
    w0 = time.time()
    with cf.ThreadPoolExecutor(a.j) as ex:
        for d, rc, err in ex.map(serve, servers):
            if rc:
                print(f"server {os.path.basename(d)}: exit {rc}:\n" + "\n".join(err.strip().splitlines()[-12:]))
    r1 = resource.getrusage(resource.RUSAGE_CHILDREN)
    cpu, wall = (r1.ru_utime + r1.ru_stime) - (r0.ru_utime + r0.ru_stime), time.time() - w0
    for t in tus:
        if t[0] not in bad and not os.path.exists(os.path.join(outdir, t[0] + ".vcc.o")):
            bad[t[0]] = "no object from its server"
    counts = {}
    with cf.ThreadPoolExecutor(a.j) as ex:
        for name, what, detail, share in ex.map(lambda t: wf.link_run(t, outdir, main_obj, a.timeout), [t for t in tus if t[0] not in bad]):
            k = "ok" if what in ("ok", "ok-unres") else what.split()[0]
            counts[k] = counts.get(k, 0) + 1
            if k != "ok" or a.v:
                print(f"{what:8} {name}  vcc has {share * 100:.0f} % of clang's functions")
                for x in detail[:3]:
                    print("         " + x[:160])
    for n, w in sorted(bad.items()):
        print(f"FAIL     {n}: {w}")
    print(f"cxx-fork: {len(tus)} TUs; tests pass {counts.get('ok', 0)}, fail {counts.get('fail', 0)}, timeout {counts.get('timeout', 0)}, "
          f"link failed {counts.get('link', 0)}, not compiled {len(bad)}")
    print(f"  vcc -O0, headers once per prefix: {cpu:.1f} s CPU ({wall:.1f} s wall, -j {a.j}); clang -O2 {CLANG_O2_CPU:.0f} s: "
          f"{CLANG_O2_CPU / max(cpu, 1e-9):.1f}x")
    return 0


if __name__ == "__main__":
    sys.exit(main())
