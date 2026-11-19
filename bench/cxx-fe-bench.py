#!/usr/bin/env python3
"""
bench/cxx-fe-bench.py: how much work the C++ front end does, as user-mode instructions.

`vpp -x c++ -fsyntax-only` (lexing, preprocessing, parsing and the whole
semantic analysis, no code generation) over the programs of
src/test/cxx/run, each run under `perf stat -e instructions:u`.  The count
is deterministic to a few thousandths of a percent, so a change to the
front end's data structures (the handles of the syntax tree, the views of
it) can be gated at a small budget where a clock could not.  Process start
and the C headers are part of every run, so a cost per node is diluted by
them: the percentages are a floor on what the analysis alone would show.

  bench/cxx-fe-bench.py [-j N] [--bin-dir DIR] [--save FILE]
  bench/cxx-fe-bench.py --against FILE [--budget PERCENT]   exit 1 over budget

Skipped without perf.
"""
import argparse
import concurrent.futures
import json
import os
import re
import shutil
import subprocess
import sys

B = os.path.dirname(os.path.abspath(__file__))
R = os.path.dirname(B)
TESTS = os.path.join(R, "src", "test", "cxx", "run")
RT = os.path.join(R, "src", "test", "rt")


def resource_dir():
    for cc in ("clang", "clang-23", "clang-22", "clang-21"):
        path = shutil.which(cc)
        if path:
            out = subprocess.run([path, "-print-resource-dir"], capture_output=True, text=True).stdout.strip()
            if out:
                return out + "/include"
    return None


def count(vpp, flags, path):
    run = subprocess.run(["perf", "stat", "-x,", "-e", "instructions:u", vpp] + flags + [path],
                         capture_output=True, text=True)
    for line in run.stderr.splitlines():
        m = re.match(r"(\d+),,instructions", line)
        if m:
            return int(m.group(1))
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("-j", type=int, default=max(1, (os.cpu_count() or 2) // 2))
    ap.add_argument("--bin-dir", default=os.environ.get("VCC_BIN") or os.path.join(R, "build", "linux", "src"))
    ap.add_argument("--save")
    ap.add_argument("--against")
    ap.add_argument("--budget", type=float, default=0.5)
    args = ap.parse_args()
    if not shutil.which("perf"):
        print("cxx-fe-bench: skipped (no perf)")
        return 0
    vpp = os.path.join(args.bin_dir, "vpp")
    res = resource_dir()
    if not res or not os.path.exists(vpp):
        print("cxx-fe-bench: needs %s and clang's resource directory" % vpp)
        return 1
    flags = ["--target=aarch64-linux-gnu", "-nostdinc", "-I", RT + "/include", "-isystem", res,
             "-x", "c++", "-std=c++20", "-fno-exceptions", "-fno-rtti", "-w", "-fsyntax-only"]
    files = sorted(os.path.join(TESTS, f) for f in os.listdir(TESTS) if f.endswith(".cpp"))
    with concurrent.futures.ThreadPoolExecutor(args.j) as pool:
        counts = list(pool.map(lambda p: count(vpp, flags, p), files))
    result = {os.path.basename(p): c for p, c in zip(files, counts) if c}
    total = sum(result.values())
    print("cxx-fe-bench: %d programs, %.1f M instructions" % (len(result), total / 1e6))
    if args.save:
        with open(args.save, "w") as f:
            json.dump({"total": total, "programs": result}, f, indent=1, sort_keys=True)
    if args.against:
        base = json.load(open(args.against))
        change = 100.0 * (total - base["total"]) / base["total"]
        print("  against %s: %+.3f%% (budget %+.2f%%)" % (args.against, change, args.budget))
        worst = sorted(((100.0 * (c - base["programs"].get(p, c)) / base["programs"].get(p, c), p)
                        for p, c in result.items()), reverse=True)[:3]
        for pct, p in worst:
            print("  %+.3f%%  %s" % (pct, p))
        if change > args.budget:
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
