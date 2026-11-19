#!/usr/bin/env python3
"""
bench/cxx-syntax-audit.py: what the type system does not tell about the syntax views.

A node (ast::node) and a token (token_index) are different types, and
neither converts to an integer or to the other without saying so: the
compiler finds a node used as a token.  Two things it cannot find, which
clang-tidy's query-based custom checks report
(src/frontend/cplusplus/syntax-audit.clang-tidy: clang-query matchers on
the real types):

  * an element's index() as a condition: it is 0 for a token, where the
    question is whether there is a child (test the element: if (x));
  * a view (16 bytes, the context included) stored in a field: records
    hold the 4-byte node or child, and the view is made where it is read
    (a file-local helper struct in an anonymous namespace may hold them).

The trap of -DVCC_CHECK_SYNTAX_VIEWS (SyntaxElement::index() of a token, a
node converted to a view of another kind), run by cxx-run.py and
cxx-sema.py, is the net for the rest.  Skipped without clang-tidy.

  bench/cxx-syntax-audit.py [-j N] [FILE...]     exit 1 if any use is found
"""
import argparse
import concurrent.futures
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FRONT = os.path.join(ROOT, "src", "frontend", "cplusplus")

CONFIG = os.path.join(FRONT, "syntax-audit.clang-tidy")

FLAGS = ["-x", "c++", "-std=gnu++20", "-fno-exceptions", "-fno-rtti",
         "-nostdinc++", "-D_GNU_SOURCE", "-I" + os.path.join(ROOT, "src", "include"),
         '-DVCC_INCDIR="/x"', "-Wno-everything"]


def find_tidy():
    for name in ("clang-tidy", "clang-tidy-23", "clang-tidy-22", "clang-tidy-21"):
        path = shutil.which(name)
        if path:
            return path
    return None


def files():
    out = subprocess.run(["git", "ls-files", "*.cpp"], cwd=FRONT,
                         capture_output=True, text=True, check=True).stdout
    skipped = ("lower/", "parse/")
    return [os.path.join(FRONT, f) for f in out.split() if not f.startswith(skipped)]


def audit(tidy, path):
    run = subprocess.run([tidy, "--experimental-custom-checks", "--quiet",
                          "--config-file=" + CONFIG, path, "--"] + FLAGS,
                         capture_output=True, text=True)
    found = []
    for line in run.stdout.splitlines() + run.stderr.splitlines():
        m = re.match(r"(.+?):(\d+):(\d+): (warning|error): (.*)", line)
        if m:
            found.append((m.group(1), int(m.group(2)), m.group(5)))
    return found


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("-j", type=int, default=os.cpu_count())
    ap.add_argument("files", nargs="*")
    args = ap.parse_args()
    tidy = find_tidy()
    if not tidy:
        print("cxx-syntax-audit: skipped (no clang-tidy)")
        return 0
    paths = [os.path.abspath(f) for f in args.files] or files()
    bad = 0
    with concurrent.futures.ThreadPoolExecutor(args.j) as pool:
        for found in pool.map(lambda p: audit(tidy, p), paths):
            for path, line, message in found:
                bad += 1
                print("%s:%d: %s" % (os.path.relpath(path, ROOT), line, message))
    print("cxx-syntax-audit: %d files, %d finding(s)" % (len(paths), bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
