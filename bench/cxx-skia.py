#!/usr/bin/env python3
"""Skia (work/skia-src/skia) through vpp -fsyntax-only: first error per TU, histogram.

  bench/cxx-skia.py [-j 6] [DIR ...]   default DIR: src/core
"""
import argparse, collections, concurrent.futures as cf, glob, os, re, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SKIA = os.environ.get("SKIA") or os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(ROOT)))
                                              if "/.claude/worktrees/" in ROOT else ROOT, "work", "skia-src", "skia")
BIN = os.environ.get("VCC_BIN") or os.path.join(ROOT, "build", "linux", "src")
FLAGS = ["-x", "c++", "-std=c++20", "-stdlib=libc++", "-I.", "-w", "-fsyntax-only"]


def one(src):
    try:
        r = subprocess.run([os.path.join(BIN, "vpp"), *FLAGS, src], cwd=SKIA, capture_output=True, text=True,
                           errors="replace", timeout=120, env={**os.environ, "VCC_CXX_PROGRESS": "1"})
    except subprocess.TimeoutExpired:
        return src, "timeout", ""
    if r.returncode == 0:
        return src, "", ""
    loc, msg = "", (r.stderr.strip().splitlines() or ["exit %d" % r.returncode])[0]
    for l in r.stderr.splitlines():
        m = re.match(r"(\S+?:\d+:\d+): error: (.*)", l)
        if m:
            loc, msg = m.group(1), m.group(2)
            break
    return src, msg, loc


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-j", type=int, default=6)
    ap.add_argument("-v", action="store_true")
    ap.add_argument("dirs", nargs="*", default=["src/core"])
    a = ap.parse_args()
    tus = sorted(f for d in a.dirs for f in glob.glob(os.path.join(SKIA, d, "*.cpp")))
    tus = [os.path.relpath(f, SKIA) for f in tus]
    with cf.ThreadPoolExecutor(a.j) as ex:
        res = list(ex.map(one, tus))
    hist = collections.Counter()
    ex1 = {}
    for src, msg, loc in res:
        if msg:
            k = re.sub(r"'[^']*'", "'X'", msg)[:100] + "  @ " + loc.rsplit(":", 2)[0] if loc else msg[:100]
            hist[k] += 1
            ex1.setdefault(k, (src, loc))
            if a.v:
                print(f"{src}: {loc}: {msg}")
    ok = sum(1 for r in res if not r[1])
    print(f"ok {ok}/{len(res)}")
    for k, n in hist.most_common(25):
        print(f"{n:4d}  {k}   e.g. {ex1[k][1]}")


if __name__ == "__main__":
    sys.exit(main())
