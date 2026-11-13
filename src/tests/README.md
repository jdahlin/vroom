# vcc test suites

Run them with `bench/vcc-ctests.py -j 6` (see its docstring).  Each test is
built by clang (the oracle) and by vcc, linked with the bare-metal runtime in
`rt/` and run under `qemu-system-aarch64 -M virt`; vcc passes when it prints
and exits like clang's build.

| dir | what | source | upstream commit | license |
|---|---|---|---|---|
| `gcc-torture/execute/` | gcc.c-torture/execute: all 1701 top-level `.c` (+ its 2 `.h`), not `builtins/` or `ieee/` | https://github.com/gcc-mirror/gcc `gcc/testsuite/gcc.c-torture/execute` | `e6bf7e6fa54d131346c1772f1ef8d33d7c01bf06` (2026-11-08) | GPL-3.0-or-later (`gcc-torture/COPYING3`) |
| `tcc-tests2/` | tinycc `tests/tests2`, unchanged (145 tests, `.expect`, Makefile) | https://repo.or.cz/tinycc.git `tests/tests2` | `70adbb129760b77e3bdfec447c1f986def763f2c` (2026-11-07) | LGPL-2.1 (`tcc-tests2/COPYING`); picoc-derived parts BSD-3 (`tcc-tests2/LICENSE`) |
| `inline/gcc/` | inlining and IPA run tests (107): `gcc.dg/ipa` (`dg-do run`), `gcc.dg/torture` (named `*inl*`, or with `always_inline`/`flatten`), `gcc.dg/*inline*`, `c-c++-common/torture` (with `inline`/`always_inline`/`flatten`); multi-file and LTO tests left out.  A file is `DIR-NAME.c` for `DIR/NAME.c` | https://github.com/gcc-mirror/gcc `gcc/testsuite` | `917da9c6e019407c9d469455ad4c50c062470d6c` | GPL-3.0-or-later (`inline/gcc/COPYING3`) |
| `inline/llvm/` | llvm-test-suite's inliner run tests: `2004-08-12-InlinerAndAllocas`, `2008-07-13-InlineSetjmp`, `extern-inline-redef` (clang's own inlining tests are FileCheck on IR, nothing to run) | https://github.com/llvm/llvm-test-suite `SingleSource` | `2ecf20c22c4c25051026fa450e0b0beb7d922209` | Apache-2.0 WITH LLVM-exception (`inline/llvm/LICENSE.TXT`) |
| `rt/` | our runtime: start.S, link.ld, a tiny libc (rt.c, include/) | here | | |
| `emit/` | emitter tests (run.sh) | here | | |
| `vcc/` | our regression tests, one per bug found outside the kernel (`--suite vcc`; self-checking: exit 0 or `abort()`, output compared with clang's) | here | | |
| `vas/` | assembler encodings checked against llvm-mc: `bench/vas-enc.py $PWD/src/tests/vas` | here | | |

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

## External suites, not vendored (`work/testsuites/`)

`bench/fetch-testsuites.sh [NAME...]` downloads these (shallow, sparse) at
the commits pinned in the script; `work/testsuites/MANIFEST` records what is
there.  About 320 MB, half a minute.  `bench/vcc-verify.py` runs clang's -verify
tests (TODO.md T20); see TEST.plan for the other suite harnesses.

| name | what (C files) | license |
|---|---|---|
| `llvm` | clang `test/C` (per standard and WG14 paper, `drs/` per Defect Report; 150, 123 with `-verify`), `test/Sema` (1195), `test/Preprocessor` (325), `test/Lexer` (84), `test/Parser` (118); `utils/ABITest` (ABI test generator).  `-verify` files carry their expected diagnostics as `// expected-error {{...}}` | Apache-2.0 WITH LLVM-exception |
| `gcc` | `gcc.c-torture/compile` (2016), `execute` incl. `ieee/` and `builtins/` (1926), `gcc.dg` (19792: 3338 `dg-do run`, 1720 with `dg-error`, `torture/` 2261, `compat/` struct-layout generator), `c-c++-common` (3436), `gcc.target/aarch64` (9681), `lib/` (dejagnu helpers) | GPL-3.0-or-later |
| `c-testsuite` | small self-checking programs with expected output (220) | per test (`.c.tags`), runner under its own LICENSE |
| `llvm-test-suite` | `SingleSource/` UnitTests, Regression/C, Benchmarks, with reference outputs (2520) | Apache-2.0 WITH LLVM-exception |
| `csmith`, `yarpgen` | random-program generators (T10); source only, not built | BSD-2 / Apache-2.0 |
| `kefir` | 875 end2end host/test runtime pairs (1764 C files); 231 compiler-internal snapshot directories inventoried separately. `bench/vcc-kefir.py`: Clang oracle and four host/test compiler combinations on Linux | GPL-3.0-only; runtime headers BSD-3-Clause |
| `libc-test` | musl's libc tests: functional, regression, math edge cases (466) | MIT |
| `chibicc`, `cproc` | the compilers' own C11 tests (50, 213) | MIT / ISC |

Kefir source provenance, migration, licenses and mapped upstream contracts are
recorded in `bench/kefir-source.json`. Its baseline requires the clean pinned
checkout; artifacts retain the immutable source snapshot, hashes, raw profiles
and every compile/link/run command. Unsupported capabilities and oracle errors
are reported separately from candidate failures.

## Updating

```
git clone --depth 1 --filter=blob:none --sparse https://github.com/gcc-mirror/gcc
(cd gcc && git sparse-checkout set gcc/testsuite/gcc.c-torture/execute)
rm src/tests/gcc-torture/execute/*
cp gcc/gcc/testsuite/gcc.c-torture/execute/*.[ch] src/tests/gcc-torture/execute/
git clone --depth 1 https://repo.or.cz/tinycc.git
rm -r src/tests/tcc-tests2; mkdir src/tests/tcc-tests2
cp tinycc/tests/tests2/* src/tests/tcc-tests2/; cp tinycc/COPYING src/tests/tcc-tests2/
```
Then update the commits above and the `TCC_SKIP` list in the runner (it
mirrors the SKIP/FLAGS lines in `tcc-tests2/Makefile`).

The inline suite, from `bench/fetch-testsuites.sh gcc llvm-test-suite`:
```
G=work/testsuites/gcc/gcc/testsuite L=work/testsuites/llvm-test-suite D=src/tests/inline
{ grep -l 'dg-do run' $G/gcc.dg/ipa/*.c; ls $G/gcc.dg/torture/*inl*.c | xargs grep -l 'dg-do run'
  grep -l 'dg-do run' $G/gcc.dg/torture/*.c | xargs grep -l 'always_inline\|flatten'
  ls $G/gcc.dg/*inline*.c | xargs grep -l 'dg-do run'
  ls $G/c-c++-common/torture/*.c | xargs grep -l 'dg-do run' | xargs grep -l 'always_inline\|flatten\|inline'
} | sort -u | while read f; do cp "$f" "$D/gcc/$(echo ${f#$G/} | tr / -)"; done
grep -l 'dg-additional-sources\|dg-do link\|lto' $D/gcc/*.c | xargs rm -f
cp $L/SingleSource/Regression/C/2004-08-12-InlinerAndAllocas.c $L/SingleSource/UnitTests/2008-07-13-InlineSetjmp.c \
   $L/SingleSource/UnitTests/Integer/extern-inline-redef.c $D/llvm/
```
The runner keeps from their `dg-options` only what clang also understands
(`INLINE_FLAGS`: gcc's pass switches such as `-fipa-pta` only steer gcc), and
lists compiler-specific tests (`INLINE_SKIP`) and known gaps (`INLINE_XFAIL`).

## The runtime

`rt/rt.c` is enough libc for these tests: printf family (with %f/%e/%g),
string and ctype functions, a bump malloc, exit/atexit/abort, setjmp/longjmp
(in start.S), qsort.  stdout and stderr both go to the UART.  There's no
compiler-rt for aarch64-linux here, so clang's build of a test that does
`long double` arithmetic fails to link and the test is skipped (ref-fail).

## Beginner diagnostic repair stories

`onboarding/` contains 25 broken examples, 25 justified repairs and 25 intentional
neighbors. `bench/vcc-onboarding.py` checks semantic message contracts, locations,
structured output, and clean compilation of the repairs/neighbors on Linux x86 and
ARM. Missing diagnostic coverage is explicit; see [the corpus README](onboarding/README.md)
for the distinction between a strict run and the known-gap CI baseline.
