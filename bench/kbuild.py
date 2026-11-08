#!/usr/bin/env python3
"""Kallt kärnbygge med fasmätning.

    python3 bench/kbuild.py [--config env/vroom-min.config] [--out work/build-x]
                            [-j 10] [--kcflags "-O1"] [-- extra make-argument]

Tar bort byggkatalogen, skapar konfigurationen (allnoconfig + fragment) och bygger
`Image`. Varje rad i make-utdata tidsstämplas. Skriver en sammanfattning med
total väggtid, CPU-tid och när kompileringsfasen startar/slutar.
"""
import argparse
import os
import resource
import shutil
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
KSRC = os.environ.get("VROOM_KSRC", os.path.join(ROOT, "work/linux"))
B = "/opt/homebrew/opt"
PATH = ":".join(f"{B}/{p}" for p in [
    "make/libexec/gnubin", "gnu-sed/libexec/gnubin", "findutils/libexec/gnubin",
    "coreutils/libexec/gnubin", "grep/libexec/gnubin", "bison/bin", "flex/bin",
    "llvm/bin", "lld/bin"]) + ":" + os.environ["PATH"]
HINC = os.path.join(ROOT, "env/host-include")


def build_config(mk, q, a, configs, env):
    subprocess.run(mk + ["allnoconfig"], **q)
    subprocess.run([os.path.join(KSRC, "scripts/kconfig/merge_config.sh"), "-m", "-O", a.out,
                    os.path.join(a.out, ".config")] + configs, cwd=KSRC, env=env,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
    subprocess.run(mk + ["olddefconfig"], **q)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--config", action="append", default=None)
    ap.add_argument("--out", default=os.path.join(ROOT, "work/build-x"))
    ap.add_argument("-j", type=int, default=os.cpu_count())
    ap.add_argument("--kcflags", default="")
    ap.add_argument("--dotconfig", help="färdig .config; hoppar över allnoconfig/merge/olddefconfig")
    ap.add_argument("--cc", help="CC och HOSTCC (t.ex. build/ccs/vroom-cc)")
    ap.add_argument("--keep", action="store_true", help="ta inte bort byggkatalogen")
    ap.add_argument("--target", default="Image")
    ap.add_argument("extra", nargs="*")
    a = ap.parse_args()
    configs = [os.path.abspath(c) for c in (a.config or [os.path.join(ROOT, "env/vroom-min.config")])]
    a.out = os.path.abspath(a.out)

    env = dict(os.environ, PATH=PATH,
               KBUILD_BUILD_TIMESTAMP="2026-11-01 00:00:00",
               KBUILD_BUILD_USER="vroom", KBUILD_BUILD_HOST="vroom")
    mk = ["make", "-C", KSRC, f"O={a.out}", "ARCH=arm64", "LLVM=1",
          f"HOSTCFLAGS=-I{HINC} -include {HINC}/vroom-prelude.h {os.environ.get('VROOM_HOSTOPT', '')}".rstrip()]
    if a.kcflags:
        mk.append(f"KCFLAGS={a.kcflags}")
    if a.cc:
        cc = os.path.abspath(a.cc)
        mk += [f"CC={cc}", f"HOSTCC={cc}"]
    mk.append("RUSTC=/usr/bin/false")

    t0 = time.monotonic()
    if not a.keep and os.path.exists(a.out):
        shutil.rmtree(a.out)
    os.makedirs(a.out, exist_ok=True)
    q = dict(env=env, stdout=subprocess.DEVNULL, check=True)
    if a.dotconfig:
        shutil.copy(a.dotconfig, os.path.join(a.out, ".config"))
        configs = [os.path.abspath(a.dotconfig)]
    else:
        build_config(mk, q, a, configs, env)
    t_cfg = time.monotonic() - t0


    log = open(a.out + ".log", "w")
    p = subprocess.Popen(mk + [f"-j{a.j}", a.target] + a.extra, env=env,
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    first_cc = last_cc = None
    n_cc = 0
    for line in p.stdout:
        t = time.monotonic() - t0
        log.write(f"{t:8.3f} {line}")
        if line.startswith("  CC "):
            n_cc += 1
            first_cc = first_cc or t
            last_cc = t
    rc = p.wait()
    total = time.monotonic() - t0
    ru = resource.getrusage(resource.RUSAGE_CHILDREN)
    log.close()

    summary = (f"exit={rc} total={total:.1f}s config={t_cfg:.1f}s "
               f"first_CC={first_cc or 0:.1f}s last_CC={last_cc or 0:.1f}s "
               f"tail={total - (last_cc or total):.1f}s CC={n_cc} "
               f"user={ru.ru_utime:.0f}s sys={ru.ru_stime:.0f}s "
               f"load={(ru.ru_utime + ru.ru_stime) / total:.1f}")
    print(summary)
    with open(os.path.join(ROOT, "results/kbuild-runs.txt"), "a") as f:
        f.write(f"{time.strftime('%Y-%m-%d %H:%M:%S')} out={os.path.basename(a.out)} "
                f"configs={','.join(os.path.basename(c) for c in configs)} "
                f"kcflags='{a.kcflags}' cc={a.cc or 'clang'} j={a.j} {summary}\n")
    if rc:
        os.system(f"grep -E -i ' error|\\*\\*\\*' {a.out}.log | head -20")
    sys.exit(rc)


if __name__ == "__main__":
    main()
