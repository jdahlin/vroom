#!/usr/bin/env python3
"""Compare vcc's lexer with clang's raw lexer, token by token.

    lexdiff.py [-j N] [--clang PATH] LIST|FILE...

clang is run as `clang -cc1 -std=gnu11 -dump-raw-tokens` (the kernel's
language mode).  Whitespace and comments are dropped from its output;
StartOfLine is carried from a dropped token to the next real one, which is
what the preprocessor sees.  Compared per token: kind, cleaned spelling,
line:column and StartOfLine.
"""
import argparse
import os
import re
import subprocess
import sys
from multiprocessing import Pool

HERE = os.path.dirname(os.path.abspath(__file__))
REC = re.compile(r"^([a-z_0-9]+) +'(.*)'\s+Loc=<.*:(\d+):(\d+)>(.*)$")
WS = re.compile(r"^(?:[ ]|\\[tn]|\\01[345]|\\000)*$")
args = None


def norm(rec):
    # Error tokens (unterminated literals) that end in a splice: clang's
    # cleaning reads the newline after the token end into the spelling.
    # Compare those spellings without a trailing newline on both sides.
    if rec.startswith("unknown\t"):
        k, spell, rest = rec.split("\t", 2)
        rec = f"{k}\t{re.sub(r'(?:\\n|\\015|\\000)+$', '', spell)}\t{rest}"
    return rec


def clang_tokens(path):
    p = subprocess.run([args.clang, "-cc1", "-std=gnu11", "-triple", "aarch64-linux-gnu",
                        "-dump-raw-tokens", path],
                       stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    out, sol = [], False
    for line in p.stderr.decode("latin-1").split("\n"):
        m = REC.match(line)
        if not m:
            continue                    # diagnostics, UnClean continuation lines
        kind, spell, ln, col, rest = m.groups()
        s = "[StartOfLine]" in rest
        if kind == "comment" or (kind == "unknown" and WS.match(spell)):
            sol |= s
            continue
        out.append(norm(f"{kind}\t{spell}\t{ln}:{col}\t{int(s or sol)}"))
        sol = False
    return out


def vcc_tokens(path):
    p = subprocess.run([args.lextest, "dump", path], stdout=subprocess.PIPE)
    return [norm(l) for l in p.stdout.decode("latin-1").splitlines()]


def check(path):
    a, b = clang_tokens(path), vcc_tokens(path)
    if a == b:
        return path, len(a), None
    for i, (x, y) in enumerate(zip(a, b)):
        if x != y:
            return path, len(a), f"token {i}:\n  clang {x}\n  vcc   {y}"
    return path, len(a), f"token count clang {len(a)} vcc {len(b)}: " \
        f"next clang {a[len(b)] if len(a) > len(b) else '-'} vcc {b[len(a)] if len(b) > len(a) else '-'}"


def init(a):
    global args
    args = a


def main():
    global args
    ap = argparse.ArgumentParser()
    ap.add_argument("-j", type=int, default=os.cpu_count())
    ap.add_argument("--clang", default="/opt/homebrew/opt/llvm/bin/clang")
    ap.add_argument("--lextest", default=os.path.join(HERE, "../../../build/src/lextest"))
    ap.add_argument("--max-show", type=int, default=20)
    ap.add_argument("files", nargs="+")
    args = ap.parse_args()
    files = []
    for f in args.files:
        if f.endswith(".txt"):
            files += [l.strip() for l in open(f) if l.strip()]
        else:
            files.append(f)
    bad = toks = 0
    with Pool(args.j, initializer=init, initargs=(args,)) as pool:
        for path, n, err in pool.imap_unordered(check, files, chunksize=4):
            toks += n
            if err:
                bad += 1
                if bad <= args.max_show:
                    print(f"{path}: {err}")
    print(f"{len(files) - bad}/{len(files)} files identical, {toks} clang tokens")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
