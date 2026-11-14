#!/usr/bin/env python3
"""The json test suite with vcc's code where vcc has it, clang's where it has not (PLAN-cxx.md)

  bench/cxx-weakfill.py [-j 6] [-v] [PAT]

A shortcut to an end-to-end measurement before vcc generates everything.  Per nlohmann/json test TU:
vpp -O0 -c (timed), clang++ -O0 -c with every definition made weak (llvm-objcopy --weaken), linked
together with clang's doctest main (tests/src/unit.cpp): vcc's strong definitions win, the functions
vcc gave no code come from clang.  Then the test runs.  Prints per TU: link and run result, the share of
.text that is vcc's; and the sum of vcc's CPU time against clang -O2's (243 s, PLAN-cxx.md), raw (a lower
bound: vcc does less work than it will) and scaled by the share of clang's code it produced.
"""
import argparse, concurrent.futures as cf, importlib.util, os, re, resource, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("cp", os.path.join(HERE, "cxx-profile.py"))
cp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cp)
VPP = os.path.join(os.environ.get("VCC_BIN", os.path.join(os.path.dirname(HERE), "build", "linux", "src")), "vpp")
OBJCOPY = os.environ.get("OBJCOPY", "llvm-objcopy-23")
CLANG_O2_CPU = 243.0


def text_size(obj):
    """bytes of the object's code sections"""
    r = subprocess.run(["llvm-size-23", "-A", obj], capture_output=True, text=True)
    return sum(int(l.split()[1]) for l in r.stdout.splitlines() if l.startswith(".text"))


def prepare(tu, outdir):
    """clang's weakened object (cached)"""
    name, src, fl = tu
    ref, weak = os.path.join(outdir, name + ".clang.o"), os.path.join(outdir, name + ".weak.o")
    if not os.path.exists(weak) or os.path.getmtime(weak) < os.path.getmtime(src) or os.path.getmtime(weak) < os.path.getmtime(__file__):
        r = subprocess.run([cp.CLANG, "-stdlib=libc++", "-O0", "-c", *fl, "-w", src, "-o", ref], capture_output=True, text=True)
        if r.returncode:
            return name, "clang failed"
        # (its functions of internal linkage too: a static test function vcc gave no code is clang's)
        subprocess.run([OBJCOPY, "--wildcard", "--globalize-symbol=_Z*", "--weaken", ref, weak], check=True)
    return name, None


def vcc(tu, outdir):
    name, src, fl = tu
    obj = os.path.join(outdir, name + ".vcc.o")
    r = subprocess.run([VPP, "-x", "c++", "-stdlib=libc++", *fl, "-w", "-c", src, "-o", obj], capture_output=True, text=True)
    return name, None if not r.returncode else "vcc failed: " + (r.stderr.strip().splitlines() or ["?"])[0]


def defined(obj):
    """the functions an object defines"""
    r = subprocess.run(["nm", "--defined-only", obj], capture_output=True, text=True)
    return set(l.split()[-1] for l in r.stdout.splitlines() if re.search(r" [TtWw] ", l))


def link_run(tu, outdir, main_obj, timeout):
    name = tu[0]
    obj, weak, exe, stubs = (os.path.join(outdir, name + s) for s in (".vcc.o", ".weak.o", ".exe", ".stubs.s"))
    cmd = [cp.CLANG, "-stdlib=libc++", main_obj, obj, weak, "-o", exe, "-Wl,--no-demangle"]
    r = subprocess.run(cmd, capture_output=True, text=True)
    und = []
    if r.returncode:
        # references nothing defines (a name vcc mangles otherwise than clang): each a stub that traps, so
        # that the run says whether anything calls them
        und = sorted(set(re.findall(r"undefined reference to `([^']+)'", r.stderr)))
        if not und:
            return name, "link", r.stderr.strip().splitlines()[-1:], 0
        with open(stubs, "w") as f:
            for i, u in enumerate(und):
                # (it says which name it is on stderr, then traps)
                msg = f"vcc stub called: {u}\n"
                f.write(f'.section .rodata\n.Lstub{i}: .ascii "{msg.encode("unicode_escape").decode()}"\n.text\n.globl "{u}"\n.type "{u}",@function\n"{u}":\n'
                        f'\tmovl $1, %eax\n\tmovl $2, %edi\n\tleaq .Lstub{i}(%rip), %rsi\n\tmovl ${len(msg)}, %edx\n\tsyscall\n\tud2\n')
        r = subprocess.run(cmd + [stubs], capture_output=True, text=True)
        if r.returncode:
            return name, "link", r.stderr.strip().splitlines()[-3:], 0
    have, need = defined(obj), defined(os.path.join(outdir, name + ".clang.o"))
    share = len(have & need) / max(1, len(need))
    try:
        r = subprocess.run([exe], capture_output=True, text=True, errors="replace", timeout=timeout, cwd=os.path.dirname(tu[1]))
    except subprocess.TimeoutExpired:
        return name, "timeout", [], share
    if r.returncode:
        tail = [l for l in r.stderr.splitlines() if l.startswith("vcc stub called")][:1] + [l for l in (r.stdout + r.stderr).splitlines() if l.strip()][-2:]
        return name, "fail %d" % r.returncode, (["%d stubbed" % len(und)] if und else []) + tail, share
    return name, "ok" if not und else "ok-unres", ["%d stubbed: " % len(und) + und[0][:100]] if und else [], share


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-j", type=int, default=6)
    ap.add_argument("-v", action="store_true")
    ap.add_argument("--timeout", type=int, default=300)
    ap.add_argument("pat", nargs="?", default="")
    a = ap.parse_args()
    tus = [t for t in cp.tus("json") if a.pat in t[0] and t[0] != "unit"]	# (unit.cpp: doctest's main)
    outdir = os.path.join(cp.outdir("json", "vcc"), "weakfill")
    os.makedirs(outdir, exist_ok=True)
    fl = tus[0][2] if tus else []
    main_obj = os.path.join(outdir, "unit-main.o")
    if not os.path.exists(main_obj):
        subprocess.run([cp.CLANG, "-stdlib=libc++", "-O0", "-c", *fl, "-w", os.path.join(os.path.dirname(tus[0][1]), "unit.cpp"),
                        "-o", main_obj], check=True)
    with cf.ThreadPoolExecutor(a.j) as ex:
        bad = {n: w for n, w in ex.map(lambda t: prepare(t, outdir), tus) if w}
    # vcc alone, its CPU time (the children's, this phase only)
    r0 = resource.getrusage(resource.RUSAGE_CHILDREN)
    w0 = time.time()
    with cf.ThreadPoolExecutor(a.j) as ex:
        bad.update({n: w for n, w in ex.map(lambda t: vcc(t, outdir), tus) if w})
    r1 = resource.getrusage(resource.RUSAGE_CHILDREN)
    cpu, wall = (r1.ru_utime + r1.ru_stime) - (r0.ru_utime + r0.ru_stime), time.time() - w0
    counts, shares = {}, []
    with cf.ThreadPoolExecutor(a.j) as ex:
        for name, what, detail, share in ex.map(lambda t: link_run(t, outdir, main_obj, a.timeout), [t for t in tus if t[0] not in bad]):
            counts[what.split()[0]] = counts.get(what.split()[0], 0) + 1
            if share:
                shares.append(share)
            if what not in ("ok", "ok-unres") or a.v:
                print(f"{what:8} {name}  vcc has {share * 100:.0f} % of clang's functions")
                for d in detail if a.v or what != "link" else detail[:3]:
                    print("         " + d[:160])
    for n, w in sorted(bad.items()):
        print(f"FAIL     {n}: {w}")
    share = sum(shares) / len(shares) if shares else 0
    print(f"cxx-weakfill: {len(tus)} TUs; ok {counts.get('ok', 0)}, ok with stubbed references {counts.get('ok-unres', 0)}, test failed {counts.get('fail', 0)}, "
          f"timeout {counts.get('timeout', 0)}, link failed {counts.get('link', 0)}, not compiled {len(bad)}")
    print(f"  vcc -O0: {cpu:.1f} s CPU ({wall:.1f} s wall, -j {a.j}); clang -O2 {CLANG_O2_CPU:.0f} s: {CLANG_O2_CPU / max(cpu, 1e-9):.1f}x raw")
    if share:
        print(f"  vcc defines {share * 100:.1f} % of the functions clang -O0 does (mean per TU); scaled by it: {cpu / share:.1f} s, "
              f"{CLANG_O2_CPU / (cpu / share):.1f}x (a rough estimate: what vcc skips is not a uniform sample)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
