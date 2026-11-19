#!/usr/bin/env python3
"""
bench/cxx-syntax-audit.py: token and node indexes mixed up through the syntax views.

SyntaxElement::index() is the node (0 for a token) and token() the token
(0 for a node); both are plain uint32_t, so passing the wrong one compiles
and, at run time, hands over 0.  clang-query matches on the real types:

  * an index() passed or assigned where a token_index is expected;
  * a token() passed or assigned where a node_index is expected.

(Only direct uses are found: through `auto` or a field typed uint32_t the
trap of -DVCC_CHECK_SYNTAX_VIEWS, run by cxx-run.py and cxx-sema.py, is the
net.)  Skipped without clang-query.

  bench/cxx-syntax-audit.py [-j N] [FILE...]     exit 1 if any use is found
"""
import argparse
import concurrent.futures
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FRONT = os.path.join(ROOT, "src", "frontend", "cplusplus")

ELEMENT_INDEX = ('cxxMemberCallExpr(callee(cxxMethodDecl(hasName("index"), '
                 'ofClass(cxxRecordDecl(hasName("SyntaxElement"))))))')
ELEMENT_TOKEN = ('cxxMemberCallExpr(callee(cxxMethodDecl(hasName("token"), '
                 'ofClass(cxxRecordDecl(hasName("SyntaxElement"))))))')


def typed(name):
    return 'typedefNameDecl(hasName("%s"))' % name


def parameter(name):
    return 'parmVarDecl(hasType(%s))' % typed(name)


QUERIES = [
    ("index() for a token_index parameter",
     'callExpr(forEachArgumentWithParam(ignoringImpCasts(%s), %s))'
     % (ELEMENT_INDEX, parameter("token_index"))),
    ("index() for a token_index parameter (constructor)",
     'cxxConstructExpr(forEachArgumentWithParam(ignoringImpCasts(%s), %s))'
     % (ELEMENT_INDEX, parameter("token_index"))),
    ("index() initializing a token_index",
     'varDecl(hasType(%s), hasInitializer(ignoringImpCasts(%s)))'
     % (typed("token_index"), ELEMENT_INDEX)),
    ("index() assigned to a token_index",
     'binaryOperator(isAssignmentOperator(), hasLHS(hasType(%s)), '
     'hasRHS(ignoringImpCasts(%s)))' % (typed("token_index"), ELEMENT_INDEX)),
    ("index() assigned to a token_index field",
     'binaryOperator(isAssignmentOperator(), hasLHS(memberExpr(hasDeclaration('
     'fieldDecl(hasType(%s))))), hasRHS(ignoringImpCasts(%s)))'
     % (typed("token_index"), ELEMENT_INDEX)),
    ("token() for a node_index parameter",
     'callExpr(forEachArgumentWithParam(ignoringImpCasts(%s), %s))'
     % (ELEMENT_TOKEN, parameter("node_index"))),
    ("token() initializing a node_index",
     'varDecl(hasType(%s), hasInitializer(ignoringImpCasts(%s)))'
     % (typed("node_index"), ELEMENT_TOKEN)),
]

FLAGS = ["-x", "c++", "-std=gnu++20", "-fno-exceptions", "-fno-rtti",
         "-nostdinc++", "-D_GNU_SOURCE", "-I" + os.path.join(ROOT, "src", "include"),
         '-DVCC_INCDIR="/x"', "-Wno-everything"]


def find_query():
    for name in ("clang-query", "clang-query-23", "clang-query-22", "clang-query-21"):
        path = shutil.which(name)
        if path:
            return path
    return None


def files():
    out = subprocess.run(["git", "ls-files", "*.cpp"], cwd=FRONT,
                         capture_output=True, text=True, check=True).stdout
    skipped = ("lower/", "parse/")
    return [os.path.join(FRONT, f) for f in out.split() if not f.startswith(skipped)]


def audit(query, script, path):
    run = subprocess.run([query, "-f", script, path, "--"] + FLAGS,
                         capture_output=True, text=True)
    found = []
    label = None
    for line in run.stdout.splitlines() + run.stderr.splitlines():
        m = re.match(r"(\d+) matches?\.", line)
        if m:
            continue
        m = re.match(r"(.+?):(\d+):(\d+): note: \"root\" binds here", line)
        if m:
            found.append((m.group(1), int(m.group(2))))
        elif ": error:" in line:
            found.append((path, 0, line))
    return found


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("-j", type=int, default=os.cpu_count())
    ap.add_argument("files", nargs="*")
    args = ap.parse_args()
    query = find_query()
    if not query:
        print("cxx-syntax-audit: skipped (no clang-query)")
        return 0
    paths = [os.path.abspath(f) for f in args.files] or files()
    with tempfile.TemporaryDirectory() as tmp:
        script = os.path.join(tmp, "audit.txt")
        with open(script, "w") as f:
            f.write("set output diag\n")
            for _, q in QUERIES:
                f.write("m %s\n" % q)
        bad = 0
        with concurrent.futures.ThreadPoolExecutor(args.j) as pool:
            for path, found in zip(paths, pool.map(lambda p: audit(query, script, p), paths)):
                for item in found:
                    bad += 1
                    where = os.path.relpath(item[0], ROOT)
                    if len(item) == 3:
                        print("%s: %s" % (where, item[2]))
                    else:
                        print("%s:%d: a token's index() or a node's token()" % (where, item[1]))
    print("cxx-syntax-audit: %d files, %d finding(s)" % (len(paths), bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
