#!/usr/bin/env python3
"""The compiler builtins libc++ uses and whether the C++ front end has them.

Scans libc++'s headers (default: Homebrew LLVM's include/c++/v1) for
__builtin_* names and the clang keywords among __is_*, __has_*, __remove_*,
... (those clang's __has_builtin answers 1 for: ext/clang/feature-checks.inc),
then probes each with vpp -x c++ -std=c++20 -fsyntax-only: a call, a type
trait with one or two type arguments, or a type transformation.  A name
that a probe accepts, or that src/frontend/cplusplus names in its source, is
had.  A name libc++ tests with __has_builtin is "guarded": libc++ has a
fallback when the answer is 0.

    bench/cxx-libcxx-builtins.py [--libcxx DIR] [--bin-dir DIR] [-v]

prints "used N, had N, missing N" and the missing names (guarded marked).
"""
import argparse
import concurrent.futures
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CXX = os.path.join(ROOT, "src", "frontend", "cplusplus")


def clang_builtins():
    names = set()
    path = os.path.join(CXX, "ext", "clang", "feature-checks.inc")
    for m in re.finditer(r'FEATURE_CHECK\(builtin, "(\w+)"', open(path).read()):
        names.add(m.group(1))
    return names


def libcxx_uses(root, known):
    used, guarded = set(), set()
    for d, _, files in os.walk(root):
        for f in files:
            try:
                text = open(os.path.join(d, f), errors="replace").read()
            except OSError:
                continue
            for m in re.finditer(r"__has_builtin\(\s*(\w+)\s*\)", text):
                guarded.add(m.group(1))
            text = re.sub(r"__has_builtin\(\s*\w+\s*\)", "", text)
            for m in re.finditer(r"\b(__\w+)\s*\(", text):
                n = m.group(1)
                if n in known or n.startswith("__builtin_") and n in known:
                    used.add(n)
    return used, guarded & known


def source_names():
    """identifiers the front end's own (not generated) source spells"""
    out = set()
    for d, _, files in os.walk(CXX):
        for f in files:
            # (feature_checks.cpp names the ones it answers 0 for)
            if (f.endswith(".cpp") or f.endswith(".h")) and f != "feature_checks.cpp":
                out.update(re.findall(r'"(__\w+)"', open(os.path.join(d, f)).read()))
    return out


PROBES = [
    "void f(){ (void){n}; }",
    "bool b = {n}(int);",
    "bool b = {n}(int, int);",
    "using T = {n}(int);",
]


def probe(vpp, n):
    with tempfile.TemporaryDirectory() as tmp:
        src = os.path.join(tmp, "t.cpp")
        for p in PROBES:
            open(src, "w").write(p.replace("{n}", n) + "\n")
            r = subprocess.run([vpp, "-x", "c++", "-std=c++20", "-fsyntax-only", src],
                               capture_output=True)
            if r.returncode == 0:
                return True
    return False


def answers_zero(vpp, n):
    with tempfile.TemporaryDirectory() as tmp:
        src = os.path.join(tmp, "t.cpp")
        open(src, "w").write(f"#if __has_builtin({n})\n#error\n#endif\n")
        r = subprocess.run([vpp, "-x", "c++", "-std=c++20", "-fsyntax-only", src],
                           capture_output=True)
        return r.returncode == 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--libcxx", default="/opt/homebrew/opt/llvm/include/c++/v1")
    ap.add_argument("--bin-dir", default=os.environ.get("VCC_BIN", os.path.join(ROOT, "build", "src")))
    ap.add_argument("-v", action="store_true", help="list the had names too")
    ap.add_argument("-j", type=int, default=6)
    a = ap.parse_args()
    known = clang_builtins()
    used, guarded = libcxx_uses(a.libcxx, known)
    spelled = source_names()
    vpp = os.path.join(a.bin_dir, "vpp")
    rest = sorted(n for n in used if n not in spelled)
    with concurrent.futures.ThreadPoolExecutor(a.j) as ex:
        ok = dict(zip(rest, ex.map(lambda n: probe(vpp, n), rest)))
    missing = sorted(n for n in rest if not ok[n])
    with concurrent.futures.ThreadPoolExecutor(a.j) as ex:
        zero = dict(zip(missing, ex.map(lambda n: answers_zero(vpp, n), missing)))
    falls_back = [n for n in missing if n in guarded and zero[n]]
    print(f"used {len(used)}, had {len(used) - len(missing)}, missing {len(missing)}: "
          f"{len(falls_back)} answered 0 by __has_builtin where libc++ falls back, "
          f"{len(missing) - len(falls_back)} left")
    for n in missing:
        note = ""
        if n in guarded:
            note = "  (guarded, __has_builtin " + ("0)" if zero[n] else "1: wrong)")
        print(f"  {n}{note}")
    if a.v:
        for n in sorted(used):
            if n not in missing:
                print(f"  had {n}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
