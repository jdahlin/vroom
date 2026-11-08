#!/usr/bin/env python3
"""vsh/rsh against dash: each script runs in two fresh copies of a small tree,
once through vsh (vccd's rsh) and once through /bin/dash; status, stdout and
the files left behind must agree (stderr too, unless the case says not).

Scripts rsh must run itself get VSH_REAL=/bin/false, so a fallback shows up as
a mismatch; scripts it must decline get VSH_REAL=/bin/dash and are checked to
have gone to the shell (vccd's sh-decl count).

  bench/vsh-test.py [--bin-dir DIR]
"""
import argparse, os, shutil, subprocess, sys, tempfile, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from hostenv import BUILD  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument("--bin-dir", default=os.environ.get("VCC_BIN", BUILD))
a = ap.parse_args()
B = os.path.abspath(a.bin_dir)
VCC, VAR, VSH = f"{B}/vcc", f"{B}/var", f"{B}/vsh/sh"
DASH = shutil.which("dash") or "/bin/dash"
# kbuild's own fixdep, from any built tree (the path must end in scripts/basic/fixdep)
FIXDEP = os.environ.get("FIXDEP") or next(
    (p for p in (os.path.join(os.path.dirname(B.rstrip("/")), "..", "work", t, "scripts/basic/fixdep")
                 for t in ("build-vsh", "build-o0", "build-linux")) if os.path.exists(p)), None)

SRC = {"x.c": "int x(void) { return 1; }\n", "y.c": "int y;\n",
       "bad.c": "int bad( {\n", "sub/keep": "k\n", ".w.d": "w: x.c sub/keep\n"}
CC = (f"set -e;  echo '  CC      x.o';   trap 'rm -f x.o; trap - HUP; kill -s HUP $$' HUP;"
      f"  trap 'rm -f x.o; trap - INT; kill -s INT $$' INT; VCC_FIXDEP_D=.x.o.d VCC_FIXDEP_T=x.o"
      f" VCC_FIXDEP_O=.x.o.cmd VCC_FIXDEP_CMD='{VCC} -Wp,-MMD,.x.o.d -O2 -c -o x.o x.c'"
      f" {VCC} -Wp,-MMD,.x.o.d -O2 -c -o x.o x.c")
# (name, script, rsh must run it, compare stderr)
CASES = [
    # first, while vccd has compiled nothing (kbuild's first fixdep follows a forwarded host link)
    ("fixdep first", f"set -e; {FIXDEP} .w.d w 'cc -o w x.c' > .w.cmd; rm -f .w.d", True, True),
    ("cc+fixdep", CC, True, True),
    ("cc plain + savedcmd", f"set -e; echo '  CC y.o'; {VCC} -c -o y.o y.c; printf '%s\\n' 'savedcmd_y.o := {VCC} -c -o y.o y.c' > .y.o.cmd", True, True),
    ("ar with members", f"set -e; {VCC} -c -o x.o x.c; {VCC} -c -o y.o y.c; echo '  AR      built-in.a'; rm -f built-in.a;  printf \"./%s \" x.o y.o | xargs {VAR} cDPrST built-in.a; printf '%s\\n' 'savedcmd_built-in.a := rm -f built-in.a;  printf \"./%s \" x.o y.o | xargs {VAR} cDPrST built-in.a' > .built-in.a.cmd", True, True),
    ("ar empty", f"set -e; rm -f e.a; {VAR} cDPrST e.a; printf '%s\\n' 'savedcmd_e.a := x' > .e.a.cmd", True, True),
    ("printf reuse", "printf '<%s>\\n' a b c; printf 'n%%\\n'; printf '%s-%s\\n' 1", True, True),
    ("echo words", "echo  a   'b  c' \"d\"e ; : ignored $; true", False, True),
    ("echo words ok", "echo  a   'b  c' \"d\"e ; : ignored; true", True, True),
    ("mkdir -p", "mkdir -p a/b//c/ sub d; mkdir -p d/e; echo ok", True, True),
    ("mkdir -p over file", "mkdir -p x.c/y; echo st", True, False),
    ("mkdir -p file", "set -e; mkdir -p x.c; echo after", True, False),
    ("rm missing ok", "rm -f nope sub/nope; echo done", True, True),
    ("rm dir fails", "set -e; rm -f sub; echo after", True, False),
    ("no errexit", "rm -f sub; echo after", True, False),
    ("failing compile -e", f"set -e; {VCC} -c -o bad.o bad.c; echo after", True, False),
    ("failing compile", f"{VCC} -c -o bad.o bad.c; echo after", True, False),
    ("xargs failure", f"printf '%s ' bad.c | xargs {VCC} -c -o bad.o; echo st", True, False),
    ("redir no dir", "set -e; printf '%s\\n' x > nodir/f; echo after", True, False),
    ("assign to tool", f"FOO=1 {VCC} -c -o x.o x.c", True, True),
    ("trailing semi", "echo a;", True, True),
    ("kbuild fixdep", f"set -e; {VCC} -Wp,-MMD,.z.o.d -c -o z.o x.c; {FIXDEP} .z.o.d z.o '{VCC} -c '\\''x'\\''' > .z.o.cmd; rm -f .z.o.d", True, True),
    ("escaped quote", r"""printf '%s\n' 'a '\''"b"'\'' c' x\ y \$z > o""", True, True),
    ("escaped assign", r"""A\=1 printf x""", False, True),
    # declined: rsh must leave these to the shell
    ("dollar", "echo $HOME", False, True),
    ("and-list", "true && echo a", False, True),
    ("subshell", "(echo a)", False, True),
    ("backslash echo", "echo 'a\\tb'", False, True),
    ("glob", "echo *.c", False, True),
    ("append", "echo a >> f", False, True),
    ("fd redir", "echo a 2>f", False, True),
    ("other cmd", "cat x.c", False, True),
    ("assign builtin", "A=1 echo a", False, True),
    ("empty cmd", "echo a;; echo b", False, False),
    ("not ours", "/usr/bin/true x", False, True),
    ("printf %d", "printf '%d\\n' 3", False, True),
]

if not FIXDEP:
    print("no built scripts/basic/fixdep: skipping that case (FIXDEP=PATH)")
    CASES = [c for c in CASES if "fixdep" not in c[0]]
else:
    FIXDEP = os.path.abspath(FIXDEP)


def tree(d):
    out = {}
    for root, dirs, files in os.walk(d):
        for x in dirs:	# directories too (mkdir -p), empty or not
            out[os.path.relpath(os.path.join(root, x), d) + "/"] = b"<dir>"
        for f in files:
            p = os.path.join(root, f)
            with open(p, "rb") as fh:
                out[os.path.relpath(p, d)] = fh.read()
    return out


def run(sh, script, cwd, env):
    p = subprocess.run([sh, "-c", script], cwd=cwd, env=env, capture_output=True, stdin=subprocess.DEVNULL)
    return p.returncode, p.stdout, p.stderr


def main():
    tmp = tempfile.mkdtemp(prefix="vsh-test.")
    sock = f"{tmp}/vccd.sock"
    log = open(f"{tmp}/vccd.log", "w")
    d = subprocess.Popen([f"{B}/vccd", "--sock", sock, "--backend-sock", f"{tmp}/none.sock", "-j", "2"],
                         stderr=log, env=dict(os.environ, VCC_LOG="1"))
    for _ in range(100):
        if os.path.exists(sock):
            break
        time.sleep(0.02)
    bad = 0
    try:
        for ci, (name, script, own, cmp_err) in enumerate(CASES):
            res = []
            for sh in (VSH, DASH):
                w = f"{tmp}/{ci}{'v' if sh == VSH else 'd'}"
                for f, s in SRC.items():
                    os.makedirs(os.path.dirname(f"{w}/{f}"), exist_ok=True)
                    with open(f"{w}/{f}", "w") as fh:
                        fh.write(s)
                env = dict(os.environ, VCC_SOCK=sock, VSH_REAL="/bin/false" if own else DASH)
                rc, o, e = run(sh, script, w, env)
                files = tree(w)
                res.append((rc, o, e if cmp_err else b"", files))
            (rc1, o1, e1, f1), (rc2, o2, e2, f2) = res
            why = []
            if rc1 != rc2:
                why.append(f"status {rc1} vs dash {rc2}")
            if o1 != o2:
                why.append(f"stdout {o1!r} vs {o2!r}")
            if e1 != e2:
                why.append(f"stderr {e1!r} vs {e2!r}")
            if f1 != f2:
                diff = sorted(k for k in set(f1) | set(f2) if f1.get(k) != f2.get(k))
                why.append(f"files differ: {diff}")
            print(f"{'ok ' if not why else 'BAD'} {name}" + ("" if not why else ": " + "; ".join(why)))
            bad += bool(why)
    finally:
        d.terminate()
        d.wait()
        log.close()
    decl = sum(1 for l in open(f"{tmp}/vccd.log") if l.startswith("vccd sh-decl"))
    want = sum(1 for c in CASES if not c[2])
    if decl != want:
        print(f"BAD declined {decl} recipes, expected {want}")
        bad += 1
    print(f"{len(CASES) - bad}/{len(CASES)} ok")
    if not bad:
        shutil.rmtree(tmp)
    else:
        print("kept", tmp)
    sys.exit(1 if bad else 0)


main()
