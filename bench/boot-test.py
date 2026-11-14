#!/usr/bin/env python3
"""Boots an arm64 kernel with Debian 13 nocloud in QEMU (hvf) and waits for the login prompt.

    python3 bench/boot-test.py [--kernel work/build-min/arch/arm64/boot/Image] [--timeout 120]
    python3 bench/boot-test.py --arch x86_64 --kernel work/build-x86/arch/x86/boot/bzImage   # KVM (PLAN-x86.md X8)

Exit 0 if "login:" appears on the serial console (= userspace reached), otherwise 1.
The whole console log is saved in results/boot-<time>.log.
"""
import argparse
import base64
import os
import selectors
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WORK = os.environ.get("VROOM_WORK", os.path.join(ROOT, "work"))  # e.g. the main checkout's work/
BASE = os.path.join(WORK, "debian/debian-13-nocloud-arm64.qcow2")


CHECK_SCRIPT = r"""
for i in $(seq 120); do
  st=$(systemctl is-system-running)
  case $st in starting|initializing) sleep 0.5;; *) break;; esac
done
{
  echo VROOM_KERNEL=$(uname -r)
  echo VROOM_DEBIAN=$(cat /etc/debian_version)
  echo VROOM_STATE=$st
  echo VROOM_FAILED=$(systemctl --failed --no-legend --plain | wc -l)
  systemctl --failed --no-legend --plain | sed 's/^/VROOM_FAILED_UNIT /'
  for u in $(systemctl --failed --no-legend --plain | awk '{print $1}'); do
    journalctl -b -u "$u" --no-pager -o cat | tail -n 8 | sed "s/^/VROOM_JOURNAL $u: /"
  done
  echo VROOM_END_$((40+2))
} > /dev/console 2>&1
"""
CHECK_B64 = base64.b64encode(CHECK_SCRIPT.encode()).decode()
# Run by systemd.run=; starts the check as its own service after multi-user.target
# so that it does not itself hold the boot transaction open.
CHECK = (f"echo {CHECK_B64} | base64 -d > /run/vroom-check.sh; "
         "systemd-run --no-block --unit=vroom-check -p After=multi-user.target "
         "-p Wants=multi-user.target /bin/sh /run/vroom-check.sh")


def log_bytes(path, f):
    f.flush()
    return open(path, "rb").read()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--kernel", default=os.path.join(ROOT, "work/build-min/arch/arm64/boot/Image"))
    ap.add_argument("--timeout", type=float, default=120)
    ap.add_argument("--append", default="")
    ap.add_argument("--allow-degraded", action="store_true")
    ap.add_argument("--no-login", action="store_true", help="stop at the login prompt")
    ap.add_argument("--arch", default="arm64", choices=["arm64", "x86_64"])
    ap.add_argument("--overlay", default=os.path.join(WORK, "debian/overlay.qcow2"),
                    help="own overlay if several sessions boot at the same time")
    a = ap.parse_args()

    base = BASE if a.arch == "arm64" else os.path.join(WORK, "debian/debian-13-nocloud-amd64.qcow2")
    overlay = a.overlay
    if os.path.exists(overlay):
        os.remove(overlay)
    subprocess.run(["qemu-img", "create", "-q", "-f", "qcow2", "-F", "qcow2", "-b", base, overlay],
                   check=True)

    x86 = a.arch == "x86_64"
    cmd = ["qemu-system-x86_64", "-M", "q35", "-accel", "kvm" if os.access("/dev/kvm", os.R_OK | os.W_OK) else "tcg",
           "-cpu", "host" if os.access("/dev/kvm", os.R_OK | os.W_OK) else "max"] if x86 else \
          ["qemu-system-aarch64", "-M", "virt", "-accel", "hvf", "-cpu", "host"]
    cmd += [
           "-smp", "4", "-m", "2048", "-nographic", "-no-reboot",
           "-kernel", a.kernel,
           "-append", (f"root=/dev/vda1 rw console={'ttyS0' if x86 else 'ttyAMA0'} panic=-1 systemd.firstboot=off "
                      f"systemd.run_success_action=none systemd.run=\"/bin/sh -c '{CHECK}'\" "
                      f"{a.append}").strip(),
           "-drive", f"file={overlay},if=none,id=hd0,format=qcow2",
           "-device", "virtio-blk-pci,drive=hd0" if x86 else "virtio-blk-device,drive=hd0",
           "-device", "virtio-rng-pci" if x86 else "virtio-rng-device"]
    os.makedirs(os.path.join(ROOT, "results"), exist_ok=True)
    logpath = os.path.join(ROOT, "results", time.strftime("boot-%Y%m%d-%H%M%S.log"))
    t0 = time.monotonic()
    p = subprocess.Popen(cmd, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    sel = selectors.DefaultSelector()
    sel.register(p.stdout, selectors.EVENT_READ)
    buf = b""
    ok = False
    t_login = None
    # Userspace check without login: systemd.run= runs a command as a
    # transient systemd service; output is written to the serial console.
    state = "boot"
    with open(logpath, "wb") as log:
        while time.monotonic() - t0 < a.timeout:
            if not sel.select(timeout=1):
                if p.poll() is not None:
                    break
                continue
            chunk = os.read(p.stdout.fileno(), 65536)
            if not chunk:
                break
            log.write(chunk)
            buf = (buf + chunk)[-8192:]
            if t_login is None and b"login:" in buf:
                t_login = time.monotonic() - t0
                if a.no_login:
                    ok = True
                    break
            if b"VROOM_END_42" in buf:
                data = log_bytes(logpath, log)
                ok = b"VROOM_STATE=running" in data or (a.allow_degraded and b"VROOM_STATE=degraded" in data)
                break
    dt = time.monotonic() - t0
    p.kill()
    p.wait()
    tail = open(logpath, "rb").read().decode(errors="replace").splitlines()[-12:]
    print("\n".join(tail))
    login = f"login-prompt {t_login:.1f} s, " if t_login else ""
    print(f"\n{'BOOT OK' if ok else 'BOOT FAILED'}: {login}done {dt:.1f} s, log: {logpath}")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
