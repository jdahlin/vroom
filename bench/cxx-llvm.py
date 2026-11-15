#!/usr/bin/env python3
"""llvm-test-suite's C++ programs (SingleSource/**/*.cpp) compiled by vcc, run against clang++'s build.

  bench/cxx-llvm.py [-j 6] [--std c++98] [-v] [PAT]

Per program: clang++ -STD must accept it (else "skip: not STD"), clang++ builds and runs it (the
reference: stdout and exit status, 20 s limit); then `vpp -x c++ -STD -c`, clang++ links the
object, and the run must give the same stdout and exit status.  Prints one line per failure, the
first error per category, and the count.  Sources: bench/fetch-testsuites.sh llvm-test-suite.
Target-specific directories (X86, AMX, Vectorizer intrinsics) are left out.
"""
import argparse, collections, glob, os, re, subprocess, sys, tempfile
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SUITE = os.path.join(ROOT, "work", "testsuites", "llvm-test-suite", "SingleSource")
VPP = os.path.join(os.environ.get("VCC_BIN") or os.path.join(ROOT, "build", "linux", "src"), "vpp")
CLANG = os.environ.get("CLANGXX", "/usr/lib/llvm-23/bin/clang++")
SKIP_DIRS = ("/X86/", "/Matrix/", "/AArch64/", "/Vectorizer/")
TIMEOUT = 20


def run(cmd, **kw):
    try:
        return subprocess.run(cmd, capture_output=True, timeout=kw.pop("timeout", 120), **kw)
    except subprocess.TimeoutExpired:
        return None


def first(err):
    lines = [l for l in err.decode(errors="replace").splitlines() if not l.startswith("cxx: ") and "undefined" in l or "error" in l] or ["?"]
    return lines[0]


def one(path, std, tmp):
    rel = os.path.relpath(path, SUITE)
    base = os.path.join(tmp, rel.replace("/", "_")[:-4])
    flags = ["-std=" + std, "-stdlib=libc++", "-w", "-I", os.path.dirname(path)]
    r = run([CLANG] + flags + ["-O0", "-c", path, "-o", base + ".ref.o"])
    if r is None or r.returncode:
        return rel, "skip", "not " + std
    r = run([CLANG, base + ".ref.o", "-o", base + ".ref", "-stdlib=libc++", "-lm"])
    if r is None or r.returncode:
        return rel, "skip", "clang link"
    ref = run([base + ".ref"], cwd=os.path.dirname(path), timeout=TIMEOUT)
    if ref is None:
        return rel, "skip", "too slow"
    r = run([VPP, "-x", "c++"] + flags + ["-c", path, "-o", base + ".o"], env={**os.environ, "VCC_CXX_PROGRESS": "1"})
    if r is None:
        return rel, "fail", "vcc: timeout"
    if r.returncode:
        return rel, "fail", "vcc: " + first(r.stderr)
    m = re.search(rb"function definitions (\d+), with code (\d+)", r.stderr)
    if not m or m.group(1) != m.group(2):
        return rel, "fail", "vcc: not every function got code (%s)" % (m.group(0).decode() if m else "no count")
    r = run([CLANG, base + ".o", "-o", base, "-stdlib=libc++", "-lm"])
    if r is None or r.returncode:
        return rel, "fail", "link: " + (first(r.stderr) if r else "timeout")
    got = run([base], cwd=os.path.dirname(path), timeout=TIMEOUT * 3)
    if got is None:
        return rel, "fail", "run: timeout"
    if got.returncode != ref.returncode:
        return rel, "fail", "run: exit %d, clang's %d" % (got.returncode, ref.returncode)
    if got.stdout != ref.stdout:
        a, b = got.stdout.splitlines(), ref.stdout.splitlines()
        i = next((i for i in range(min(len(a), len(b))) if a[i] != b[i]), min(len(a), len(b)))
        return rel, "fail", "run: output differs at line %d: %r, clang's %r" % (
            i + 1, a[i][:60] if i < len(a) else b"<eof>", b[i][:60] if i < len(b) else b"<eof>")
    return rel, "ok", ""


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-j", type=int, default=6)
    ap.add_argument("--std", default="c++98")
    ap.add_argument("-v", action="store_true", help="also list skipped programs")
    ap.add_argument("pat", nargs="?", default="")
    a = ap.parse_args()
    if not os.path.isdir(SUITE):
        sys.exit("no %s: run bench/fetch-testsuites.sh llvm-test-suite" % SUITE)
    files = sorted(f for f in glob.glob(os.path.join(SUITE, "**", "*.cpp"), recursive=True)
                   if a.pat in os.path.relpath(f, SUITE) and not any(d in f for d in SKIP_DIRS))
    with tempfile.TemporaryDirectory() as tmp, ThreadPoolExecutor(a.j) as ex:
        res = list(ex.map(lambda f: one(f, a.std, tmp), files))
    count = collections.Counter(s for _, s, _ in res)
    cats = collections.Counter()
    for rel, s, why in res:
        if s == "fail" or (s == "skip" and a.v):
            print("%-4s %s: %s" % (s.upper(), rel, why))
        if s == "fail":
            cats[re.sub(r"\d+|'[^']*'|\"[^\"]*\"", "N", why.split(": ", 2)[-1])[:90]] += 1
    if cats:
        print("\nmost common:")
        for c, n in cats.most_common(10):
            print("  %3d  %s" % (n, c))
    print("cxx-llvm %s: %d/%d ok, %d skipped (%s)" % (a.std, count["ok"], count["ok"] + count["fail"], count["skip"],
          ", ".join("%s %d" % kv for kv in collections.Counter(w for _, s, w in res if s == "skip").items()) or "-"))
    return 1 if count["fail"] else 0


if __name__ == "__main__":
    sys.exit(main())
