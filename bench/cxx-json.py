#!/usr/bin/env python3
"""PLAN-cxx.md's proof of concept: how far vcc gets on nlohmann/json's tests.

  bench/cxx-json.py [-j 6] [--stage pp|syntax|obj|link|run] [-v] [PAT]

Per test TU, the stages in order, each stopping at its first failure:
  pp      vpp -E token stream equal to clang++ -E -P (vpp --cmp)
  syntax  vpp -fsyntax-only
  obj     vpp -c at -O2
  link    vpp -c at -O0 (the direct emitter: exception tables), linked by clang++ with doctest's main
          (tests/src/unit.cpp, built once by clang++)
  run     the test binary, run in tests/: its exit status 0
Prints the count per stage and the most common first errors, so the next construct to implement is
the top line.  For link, the undefined symbols ranked by how many tests they block (and how many
they alone block), each with vcc's reason it built no code for that function (VCC_CXX_GEN=4); -v
lists each test's result.
Sources: work/cxx/json (see PLAN-cxx.md X0); flags as bench/cxx-profile.py.
"""
import argparse, collections, importlib.util, os, re, subprocess, sys, threading
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("cxxprof", os.path.join(HERE, "cxx-profile.py"))
cp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cp)
BIN = os.environ.get("VCC_BIN") or os.path.join(cp.ROOT, "build", "linux", "src")
VPP = os.path.join(BIN, "vpp")
STAGES = ["pp", "syntax", "obj", "link", "run"]
CLANGXX = os.environ.get("CLANGXX", "/usr/lib/llvm-23/bin/clang++")
UNDEF = {}       # TU -> its undefined symbols (link)
NOFN = {}        # TU -> {function name: vcc's reason it has no code}
MISS = {}        # TU -> {mangled name: vcc's reason}
NOREASON = {}    # undefined symbol -> tests where vcc gave no reason for it
PROGRESS = {}    # TU -> (tokens parsed, tokens) from the syntax stage


def first_error(text):
    for l in text.splitlines():
        if "error:" in l:
            return re.sub(r"^.*?error:\s*", "", l).strip()
    return (text.strip().splitlines() or ["(no output)"])[0]


def bucket(msg):
    """The error without names and numbers, for the histogram."""
    return re.sub(r"'[^']*'", "'X'", re.sub(r"\d+", "N", msg))[:120]


MAIN_LOCK = threading.Lock()


def main_object(fl, outdir):
    """doctest's main, built by clang++ once (the json tests' flags of the first test)"""
    o = os.path.join(outdir, "unit-main.clang.o")
    with MAIN_LOCK:
        if os.path.exists(o):
            return o
        subprocess.run([CLANGXX, "-stdlib=libc++", "-O0", "-w", *fl, "-c", os.path.join(cp.CXX, "json", "tests", "src", "unit.cpp"),
                        "-o", o + ".tmp"], check=True)
        os.replace(o + ".tmp", o)
    return o


_DEM = {}


def demangle(sym):
    """a mangled name demangled (c++filt), cached"""
    if sym not in _DEM:
        r = subprocess.run(["c++filt", sym], capture_output=True, text=True)
        _DEM[sym] = r.stdout.strip() or sym
    return _DEM[sym]


def short_name(sym):
    """a demangled symbol's function name, without scopes, template arguments or parameters"""
    s, d, out = sym, 0, []
    for ch in s:
        d += ch == "<" or ch == "("
        if d == 0:
            out.append(ch)
        d -= ch == ">" or ch == ")"
    return "".join(out).split("::")[-1].split()[-1] if "".join(out).strip() else sym


def link(name, src, fl, base, outdir):
    o, exe = os.path.join(outdir, name + ".o0.o"), os.path.join(outdir, name + ".exe")
    try:
        r = subprocess.run([VPP, *base, "-O0", "-c", src, "-o", o], capture_output=True, text=True, timeout=300, errors="replace",
                           env={**os.environ, "VCC_CXX_GEN": "4", "VCC_CXX_MISSING": o + ".miss"})
    except subprocess.TimeoutExpired:
        return name, "link", "vcc: timeout"
    NOFN[name] = {}
    MISS[name] = {}     # mangled name -> vcc's reason (VCC_CXX_MISSING): exact, unlike NOFN's short names
    try:
        for l in open(o + ".miss", errors="replace"):
            m, _, why = l.rstrip("\n").partition("\t")
            MISS[name][m] = why
    except OSError:
        pass
    for l in r.stderr.splitlines():
        m = re.match(r"nofn (.*?): (.*)", l)
        if m and m.group(1).strip() not in NOFN[name]:
            NOFN[name][m.group(1).strip()] = m.group(2)
    if r.returncode:
        return name, "link", "vcc: " + first_error(r.stderr)
    r = subprocess.run([CLANGXX, "-stdlib=libc++", "-Wl,--no-demangle", main_object(fl, outdir), o, "-o", exe], capture_output=True,
                       text=True, errors="replace")
    if r.returncode:
        und = sorted(set(m.group(1) for m in re.finditer(r"undefined reference to `([^']*)'", r.stderr)))
        UNDEF[name] = und
        return name, "link", "%d undefined" % len(und) if und else first_error(r.stderr)
    return name, "ok-link", ""


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
        elif st == "obj":
            a = [VPP, *base, "-O2", "-c", src, "-o", os.path.join(outdir, name + ".o")]
        elif st == "link":
            return link(name, src, fl, base, outdir)
        else:
            exe = os.path.join(outdir, name + ".exe")
            try:
                r = subprocess.run([exe], capture_output=True, text=True, timeout=300, errors="replace",
                                   cwd=os.path.join(cp.CXX, "json", "tests"))
            except subprocess.TimeoutExpired:
                return name, st, "timeout"
            if r.returncode:
                fails = [l for l in r.stdout.splitlines() if "ERROR" in l or "FATAL" in l]
                return name, st, "exit %d: %s" % (r.returncode, (fails or r.stdout.strip().splitlines()[-1:] or [""])[0][:100])
            continue
        try:
            r = subprocess.run(a, capture_output=True, text=True, timeout=300, env={**os.environ, "VCC_CXX_PROGRESS": "1"},
                               errors="replace")
        except subprocess.TimeoutExpired:
            return name, st, "timeout"
        m = re.search(r"cxx: parsed (\d+)/(\d+) tokens", r.stderr)
        if m:
            PROGRESS[name] = (int(m.group(1)), int(m.group(2)))
        if r.returncode:
            return name, st, first_error(r.stderr + r.stdout)
    return name, "ok", ""


def one_staged(tu, last, outdir):
    name, st, msg = one(tu, last, outdir)
    if st == "ok-link":	# (link passed: run, if asked)
        if last == "run":
            return one_run(name, outdir)
        return name, "ok", ""
    return name, st, msg


# doctest options per test: the test cases json's own build leaves out on x86-64 (unit-32bit's "32bit" checks
# SIZE_MAX == 0xffffffff: json builds that TU with -m32)
RUN_ARGS = {"unit-32bit": ["--test-case-exclude=32bit"]}


def one_run(name, outdir):
    exe = os.path.join(outdir, name + ".exe")
    try:
        r = subprocess.run([exe, *RUN_ARGS.get(name, [])], capture_output=True, text=True, timeout=300, errors="replace", cwd=os.path.join(cp.CXX, "json", "tests"))
    except subprocess.TimeoutExpired:
        return name, "run", "timeout"
    if r.returncode:
        fails = [l for l in r.stdout.splitlines() if "ERROR" in l or "FATAL" in l]
        return name, "run", "exit %d: %s" % (r.returncode, (fails or r.stdout.strip().splitlines()[-1:] or [""])[0][:100])
    return name, "ok", ""


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-j", type=int, default=6)
    ap.add_argument("--stage", choices=STAGES, default="obj")
    ap.add_argument("-v", action="store_true")
    ap.add_argument("pat", nargs="?", default="")
    a = ap.parse_args()
    tus = [t for t in cp.tus("json") if a.pat in t[0]]
    outdir = cp.outdir("json", "vcc")
    with ThreadPoolExecutor(a.j) as ex:
        res = list(ex.map(lambda t: one_staged(t, a.stage, outdir), tus))
    if a.v:
        for nm, st, m in sorted(res):
            print(f"{'PASS' if st == 'ok' else 'FAIL ' + st:10} {nm}: {m}")
    at = collections.Counter(st for _, st, _ in res)
    print(f"json: {len(res)} TUs; " + ", ".join(f"{s} failed {at[s]}" for s in ["ref"] + STAGES if at[s])
          + f"; passed through {a.stage}: {at['ok']}")
    if PROGRESS:
        done = sum(a for a, _ in PROGRESS.values())
        tot = sum(b for _, b in PROGRESS.values())
        least = min(PROGRESS.items(), key=lambda kv: kv[1][0] / max(kv[1][1], 1))
        print(f"  parsed {done}/{tot} tokens ({100 * done / max(tot, 1):.1f} %); least: {least[0]} "
              f"{least[1][0]}/{least[1][1]}")
    for st in ["ref"] + STAGES:
        errs = collections.Counter(bucket(m) for _, s, m in res if s == st)
        for m, n in errs.most_common(8):
            ex_ = next(nm for nm, s, mm in res if s == st and bucket(mm) == m)
            print(f"  {st:6} {n:3}x  {m}   (e.g. {ex_})")
    if UNDEF:
        # what blocks the most tests from linking: each undefined symbol, the tests it blocks, the tests
        # it alone blocks, and vcc's reason for the function (the first test's)
        block, alone = collections.Counter(), collections.Counter()
        for nm, und in UNDEF.items():
            for u in und:
                block[u] += 1
                alone[u] += len(und) == 1
        print(f"  link: {sum(1 for u in UNDEF.values() if len(u) == 1)} tests one symbol short; the symbols blocking the most:")
        # the same by vcc's reason (a test's own functions, DOCTEST_ANON_FUNC_N, differ per test: their reasons do not)
        rb, ra, rex = collections.Counter(), collections.Counter(), {}
        for nm, und in UNDEF.items():
            seen = set()
            for u in und:
                why = MISS.get(nm, {}).get(u) or NOFN.get(nm, {}).get(short_name(demangle(u)), "? (no reason: not defined where used)")
                key = re.sub(r"\s+", " ", why)[:110]
                if key not in seen:
                    seen.add(key)
                    rb[key] += 1
                    ra[key] += len(und) == 1
                    rex.setdefault(key, nm)
                    if key.startswith("? (no reason"):
                        NOREASON[u] = NOREASON.get(u, 0) + 1
        print("  link: the reasons blocking the most tests (tests, alone), e.g.:")
        for k, n in sorted(rb.items(), key=lambda kv: (-kv[1], -ra[kv[0]]))[:25]:
            print(f"  {n:3}x ({ra[k]:2} alone)  {k}   ({rex[k]})")
        if NOREASON:
            print("  link: undefined with no reason from vcc (no nofn line for the name):")
            for u, n in sorted(NOREASON.items(), key=lambda kv: -kv[1])[:15]:
                print(f"  {n:3}x  {demangle(u)[:150]}")
        print("  link: the symbols blocking the most tests:")
        for u, n in sorted(block.items(), key=lambda kv: (-kv[1], -alone[kv[0]]))[:12]:
            why = next((MISS[nm][u] for nm in UNDEF if u in UNDEF[nm] and u in MISS.get(nm, {})), None) or \
                next((NOFN[nm][short_name(demangle(u))] for nm in UNDEF if u in UNDEF[nm] and short_name(demangle(u)) in NOFN.get(nm, {})), "?")
            print(f"  {n:3}x ({alone[u]:2} alone)  {demangle(u)[:110]}\n            {why[:150]}")
    return 0 if at["ok"] == len(res) else 1


if __name__ == "__main__":
    sys.exit(main())
