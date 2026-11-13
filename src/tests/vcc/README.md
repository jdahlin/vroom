# vcc: our own regression tests (TODO.md T1)

Self-checking programs, built by clang (the oracle) and by vcc, run under
qemu by `bench/vcc-ctests.py --suite vcc`.  A test passes when vcc's build
exits 0 and prints what clang's prints.  One file per bug; name it after
what it checks, not after the bug number.

Markers (C comments anywhere in the file):
- `/* vcc-flags: ... */` extra flags for both compilers.
- `/* vcc-timeout: S */` vcc's compile must finish within S seconds (default
  60): a bound for compile-time regressions (`init_large.c`).
- `/* vcc-xfail: <TODO.md item> */` a known vcc bug.  The runner counts the
  failure as xfail; once vcc passes, it reports XPASS as an error, so the
  commit that fixes the bug also removes the marker.

The runtime is `../rt` (bare metal, one core): no threads, no files.
Tests that need threads, the system libc or Darwin go in the host suite (T3).
