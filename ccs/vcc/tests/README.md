# vcc test suites

Run them with `bench/vcc-ctests.py -j 6` (see its docstring).  Each test is
built by clang (the oracle) and by vcc, linked with the bare-metal runtime in
`rt/` and run under `qemu-system-aarch64 -M virt`; vcc passes when it prints
and exits like clang's build.

| dir | what | source | upstream commit | license |
|---|---|---|---|---|
| `gcc-torture/execute/` | gcc.c-torture/execute: all 1701 top-level `.c` (+ its 2 `.h`), not `builtins/` or `ieee/` | https://github.com/gcc-mirror/gcc `gcc/testsuite/gcc.c-torture/execute` | `e6bf7e6fa54d131346c1772f1ef8d33d7c01bf06` (2026-11-08) | GPL-3.0-or-later (`gcc-torture/COPYING3`) |
| `tcc-tests2/` | tinycc `tests/tests2`, unchanged (145 tests, `.expect`, Makefile) | https://repo.or.cz/tinycc.git `tests/tests2` | `70adbb129760b77e3bdfec447c1f986def763f2c` (2026-11-07) | LGPL-2.1 (`tcc-tests2/COPYING`); picoc-derived parts BSD-3 (`tcc-tests2/LICENSE`) |
| `rt/` | our runtime: start.S, link.ld, a tiny libc (rt.c, include/) | here | | |
| `emit/` | emitter tests (run.sh) | here | | |
| `vcc/` | our regression tests, one per bug found outside the kernel (`--suite vcc`; self-checking: exit 0 or `abort()`, output compared with clang's) | here | | |
| `vas/` | assembler encodings checked against llvm-mc: `bench/vas-enc.py $PWD/ccs/vcc/tests/vas` | here | | |

The files are vendored as they are (no local edits), so updating is a copy.

## Why these two

- **gcc.c-torture/execute**: self-checking programs (return 0 or `abort()`),
  and no dejagnu needed apart from a few `dg-options` lines the runner reads.
  They're GNU C, the kernel's dialect: K&R definitions, statement
  expressions, nested functions, `__builtin_*`, bit-fields, `__int128`,
  vector types, computed goto.  About 25 years of real miscompiles reduced to
  small tests.
- **tcc tests2**: around 100 broader programs that print their results, so a
  wrong value shows up as a diff and not only as an abort.  Lots of
  preprocessor and declaration-syntax corners.

Not taken: `gcc.dg` (mostly dejagnu `dg-error`/`dg-final` scans of gcc's
own diagnostics and dumps), clang `test/Sema`/`CodeGen` (lit + FileCheck,
tied to clang's messages and IR), chibicc/cproc tests (small and C11-only,
and tcc tests2 covers the same ground).

## Updating

```
git clone --depth 1 --filter=blob:none --sparse https://github.com/gcc-mirror/gcc
(cd gcc && git sparse-checkout set gcc/testsuite/gcc.c-torture/execute)
rm ccs/vcc/tests/gcc-torture/execute/*
cp gcc/gcc/testsuite/gcc.c-torture/execute/*.[ch] ccs/vcc/tests/gcc-torture/execute/
git clone --depth 1 https://repo.or.cz/tinycc.git
rm -r ccs/vcc/tests/tcc-tests2; mkdir ccs/vcc/tests/tcc-tests2
cp tinycc/tests/tests2/* ccs/vcc/tests/tcc-tests2/; cp tinycc/COPYING ccs/vcc/tests/tcc-tests2/
```
Then update the commits above and the `TCC_SKIP` list in the runner (it
mirrors the SKIP/FLAGS lines in `tcc-tests2/Makefile`).

## The runtime

`rt/rt.c` is enough libc for these tests: printf family (with %f/%e/%g),
string and ctype functions, a bump malloc, exit/atexit/abort, setjmp/longjmp
(in start.S), qsort.  stdout and stderr both go to the UART.  There's no
compiler-rt for aarch64-linux here, so clang's build of a test that does
`long double` arithmetic fails to link and the test is skipped (ref-fail).
