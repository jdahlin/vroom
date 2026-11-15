#!/usr/bin/env python3
"""Run each vref command in C and C++, apply its patch and execute the result."""

import argparse
import os
from pathlib import Path
import subprocess
import tempfile

from hostenv import BUILD

SOURCE = """#include "local.h"
static int mix(int left, int right, int unused) { return 10 * left + right; }
static int run(int input) {
    int first;
    int second;
    first = input;
    first += 2;
    return mix(first, 3, 9);
}
int main(void) { return run(2) != 43; }
"""
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--bin-dir", type=Path, default=BUILD)
args = parser.parse_args()
tool = (Path(args.bin_dir) / "vref").resolve()
count = 0


def run(directory, file, source, op, where, *params, fail=False):
    global count
    (directory / file).write_text(source)
    result = subprocess.run(
        [tool, op, file, where, *params, "--", "-DTEST=1"], cwd=directory, capture_output=True
    )
    assert (result.returncode != 0) == fail, (file, op, result.stderr)
    assert (directory / file).read_text() == source
    count += 1
    if fail:
        assert not result.stdout and b"Traceback" not in result.stderr
        return
    if op == "refs":
        return result.stdout.decode().splitlines()
    assert result.stdout.startswith(b"--- a/"), (op, result.stdout)
    subprocess.run(["git", "apply", "-"], cwd=directory, input=result.stdout, check=True)
    return (directory / file).read_text()


with tempfile.TemporaryDirectory() as td:
    directory = Path(td)
    (directory / "local.h").write_text("#if TEST != 1\n#error missing flag\n#endif\n")
    for file in ("sample.c", "sample.cpp"):
        for op, where, params, expected in [
            ("rename", "6:5", ["total"], "return mix(total, 3, 9);"),
            ("change-type", "4:9", ["long"], "long first;"),
            ("delete-var", "5:9", [], "int first;\n    first = input;"),
            ("move-up", "5:9", [], "int second;\n    int first;"),
            ("move-down", "4:9", [], "int second;\n    int first;"),
            ("add-param", "8:12", ["int", "extra", "7"], "mix(first, 3, 9, 7)"),
            ("swap-param", "2:12", ["1", "2"], "mix(3, first, 9)"),
            ("remove-param", "2:12", ["3"], "mix(first, 3)"),
            ("change-param-type", "2:12", ["2", "long"], "long right"),
            ("extract", "6:5", ["7:15", "advance"], "advance(&first, &input);"),
        ]:
            changed = run(directory, file, SOURCE, op, where, *params)
            assert expected in changed, (op, changed)
            subprocess.run(
                [os.environ.get("CLANG", "clang"), "-DTEST=1", file, "-o", "check"],
                cwd=directory,
                check=True,
            )
            subprocess.run([directory / "check"], cwd=directory, check=True)
        shadow = "int n;\nint f(int n) {\n    { int n = 4; n++; }\n    return n;\n}\n"
        assert run(directory, file, shadow, "refs", "2:11") == [f"{file}:2:11", f"{file}:4:12"]
        for op, where, source, params in [
            ("rename", "3:20", SOURCE, ["first"]),
            ("rename", "3:20", SOURCE, ["while"]),
            ("delete-var", "4:9", SOURCE, []),
            ("remove-param", "2:12", SOURCE, ["1"]),
            ("remove-param", "2:12", SOURCE.replace("3, 9);", "3, first++);"), ["3"]),
            ("swap-param", "2:12", SOURCE + "void *addr = (void *)&mix;\n", ["1", "2"]),
            ("extract", "6:5", SOURCE, ["8:28", "helper"]),
            (
                "rename",
                "2:11",
                "#define BOTH(x) (#x, x)\nint f(int p) { return BOTH(p); }\n",
                ["x2"],
            ),
        ]:
            run(directory, file, source, op, where, *params, fail=True)
    cpp = "namespace ns { int f(int p) { int n(p); n++; return n; } }\n"
    assert len(run(directory, "ns.cpp", cpp, "refs", "1:35")) == 3
print(f"vref: {count} cases passed")
