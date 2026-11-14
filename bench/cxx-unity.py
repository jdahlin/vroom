#!/usr/bin/env python3
"""What sharing headers, instances and inline code across TUs buys, measured with clang (PLAN-cxx.md,
the 30x estimate): the json tests that do not reconfigure the library (no #define of JSON_*/private,
no specializations in nlohmann or std, no internal headers), each in a namespace of its own in one
unity TU, against the same tests compiled one by one.  Prints CPU seconds at -O0 and -O2.

  bench/cxx-unity.py      (sources in work/cxx/json; several minutes)
"""
import os, re, resource, subprocess, sys
R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WORK = os.environ.get("VROOM_WORK") or os.path.join(R, "work")
J = WORK + "/cxx/json"; W = WORK + "/cxx/unity"; os.makedirs(W, exist_ok=True)
FL = ["-std=c++20", "-stdlib=libc++", "-I", J + "/single_include", "-I", J + "/tests/thirdparty/doctest", "-I", J + "/tests/thirdparty/fifo_map",
      "-DDOCTEST_CONFIG_SUPER_FAST_ASSERTS", "-I", J + "/gen", "-I", J + "/tests/src", "-w"]
TS = J + "/tests/src"
excl = set()
for f in os.listdir(TS):
    t = open(os.path.join(TS, f), errors="replace").read() if f.endswith(".cpp") else ""
    if re.search(r"^\s*#\s*(define|undef)\s+(JSON_|private|protected|_LIBCPP|NDEBUG|NLOHMANN|DOCTEST)|namespace nlohmann|namespace std|^\s*#\s*pragma|nlohmann/detail", t, re.M):
        excl.add(f)
files = sorted(f for f in os.listdir(J + "/tests/src") if f.startswith("unit-") and f.endswith(".cpp") and f not in excl)
def write(fs):
    incs = []
    for f in fs:
        depth = 0
        for l in open(f"{J}/tests/src/{f}", errors="replace"):
            if re.match(r'\s*#\s*if', l):
                depth += 1
            elif re.match(r'\s*#\s*endif', l):
                depth -= 1
            elif depth == 0 and re.match(r'\s*#\s*include\s*[<"]', l) and l.strip() not in incs:
                incs.append(l.strip())
    with open(W + "/unity.cpp", "w") as o:
        o.write("\n".join(incs) + "\n")
        for i, f in enumerate(fs):
            o.write(f'namespace unity_{i} {{\n#include "{J}/tests/src/{f}"\n}}\n')
def cpu(args):
    r0 = resource.getrusage(resource.RUSAGE_CHILDREN)
    p = subprocess.run(args, capture_output=True, text=True)
    r1 = resource.getrusage(resource.RUSAGE_CHILDREN)
    return p, (r1.ru_utime - r0.ru_utime) + (r1.ru_stime - r0.ru_stime)
while True:
    write(files)
    p, _ = cpu(["clang++-23", *FL, "-fsyntax-only", "-ferror-limit=1", W + "/unity.cpp"])
    if not p.returncode:
        break
    bad = re.findall(r"tests/src/(unit-[\w-]+\.cpp):\d+", p.stderr)
    if not bad:
        print(p.stderr[:600]); sys.exit(1)
    files.remove(bad[0]); print("dropped", bad[0], file=sys.stderr)
print("unity TUs:", len(files))
open(W + "/files.txt", "w").write("\n".join(files))
for opt in ("-O0", "-O2"):
    _, u = cpu(["clang++-23", *FL, opt, "-c", W + "/unity.cpp", "-o", W + "/unity.o"])
    sep = 0.0
    for f in files:
        _, t = cpu(["clang++-23", *FL, opt, "-c", f"{J}/tests/src/{f}", "-o", W + "/one.o"])
        sep += t
    print(f"clang {opt}: separate {sep:.1f} s CPU, unity {u:.1f} s CPU, ratio {sep / u:.2f}")
