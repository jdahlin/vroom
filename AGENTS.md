# vroom: agent notes

Fast cold arm64 Linux kernel build on macOS (M1 Pro: 8 P + 2 E cores) that
boots Debian.  Phase 1 used clang; phase 2 (`PLAN-fas2.md`) replaces it with
our own toolchain, `ccs/vcc/`: lexer, preprocessor, parser, emitter,
assembler, linker, behind `vcc`/`vas`/`vld` shims and the `vccd` server.

## Goals and constraints (from the user)
- Cold build under 2 s; every part state of the art in performance (SIMD,
  parallel, minimal allocation, mmap, separate I/O and CPU threads).
- No clang dependency at run time.  Data taken from clang once (capability
  tables, predefined macros, `__has_*` answers) lives in generated files
  (`ccs/vcc/caps.c`, `ccs/vcc/ppdata*.inc` via `gen-ppdata.sh`).
- Do not replace make.
- The machine is shared: keep benchmarks short and use `-j 6` for
  parallel diff runs.

## Layout
- `PLAN.md`, `PLAN-fas2.md`: plans and per-step status (Swedish).  Update the
  status section of a step when it changes.
- `RESEARCH-preprocessor.md`: preprocessor design and prior art.
- `ccs/`: our code; `ccs/Makefile` builds into `build/ccs/`.
  - `vcc/lex.c`, `intern.c`: SIMD lexer, token cache, identifier interning.
  - `vcc/pp/`: preprocessor (pull model, clang-exact expansion).
  - `vcc/cc/`: parser + sema (GNU C11, clang's layout and constant rules).
  - `vcc/emit/`: code generator (arm64, AAPCS64, straight from the AST).
  - The three are directories of files by topic, one internal header each
    (`ppimpl.h`, `ccimpl.h`, `emitimpl.h`) with the types, macros and
    prototypes the files share; a shared function is `VPRIV` (static in
    the unity build).  Each directory is compiled as one TU by default
    (`obj/unity/DIR.c` #includes its files, SQLite style); `make
    VCC_UNITY=` compiles file by file, and every file must compile alone.
  - `vcc/vpp.c`: preprocessor driver for parity tests.
  - `vcc/shim.c`, `vccd.c`, `caps.c`: the compiler shim and server.
  - `vcc/ar.c`: the archiver (thin archives, llvm-ar-identical), run by
    vccd when the shim is called as `var`; the build uses no llvm-ar.
  - `vcc/rsh.c`: kbuild's recipe subset of sh, run by vccd for `vsh`
    (`build/ccs/vsh/sh`, shim.c with `-DVSH`), make's SHELL in
    `bench/vccd-kbuild.sh`; anything outside the subset goes to `$VSH_REAL`.
- `bench/`: harnesses.  `bench/vcc-diff.py` compares `vpp` with `clang -E`;
  `bench/vcc-sema.py` runs the front end and has clang verify its claims;
  `bench/vccd-cc.py` drives a private vccd with `-c` jobs.
- `work/` (git-ignored, case-sensitive sparsebundle): kernel source in
  `work/linux`, build tree in `work/build-o0` (TU command lines in
  `**/.*.o.cmd`), clang reference outputs in `work/ppref/`.

## Build and test
```
make -C ccs -j 6 vcc                # all vcc tools (pp/ cc/ emit/ one TU each, ccache if installed)
make -C ccs -j 6 vcc VCC_UNITY= OUT=../build/ccs-parts   # file by file: each file compiles alone
bench/vcc-diff.py ref -j 6          # clang -E references (once, ~1 min)
bench/vcc-diff.py cmp -j 6 --kernel # preprocessor parity, kernel TUs
bench/vcc-diff.py cmp -j 6          # ... including host tools (scripts/)
bench/vcc-sema.py run -j 6          # vpp -fsyntax-only on every C TU
bench/vcc-sema.py batch -j 6        # the same in one vpp --batch process: ~3 s instead of ~35 s
bench/vcc-sema.py layout -j 6       # layouts/constants/decl types checked by clang (~70 s)
bench/var-cmp.py work/build-x       # vcc ar against llvm-ar on an llvm-ar-built tree
bench/vsh-test.py                   # vsh/rsh against dash on small recipes
bench/vcc-driver.py                 # driver: command lines vs clang (predefines from -O), <1 s
bench/vcc-fpcheck.py -j 6           # no generated FP/SIMD code under -mgeneral-regs-only (~10 s)
bench/vcc-ctests.py -j 6 --suite vcc # our own regression tests (ccs/vcc/tests/vcc), ~1 s
bench/vcc-ctests.py -j 6 --cpu cortex-a53   # vcc's binaries on a CPU without LSE (atomics as LL/SC)
bench/vcc-stress.py [--vccd]        # pathological inputs fail cleanly, vccd survives (~2 s)
bench/vcc-abi.py                    # cross-compiler calls, clang/vcc x caller/callee, qemu (ccs/vcc/tests/abi, ~3 s)
bench/vcc-abi.py --host             # the same for the Apple arm64 ABI, run natively
bench/vcc-darwin-s.py -j 6           # Darwin -S assembled by Apple's as, runs as the direct object (~2 min)
bench/vcc-builtins.py [-v]          # clang builtins unknown to vcc, __has_builtin differences (<1 s)
bench/vcc-bflayout.py               # generated bit-field records, layouts checked by clang, Linux + Apple (~1 s)
bench/vcc-ldconst.py                # generated long double literals and folds, bits vs clang (~2 s)
bench/vccd-test.py                  # vccd as a service: socket, bad requests, cache invalidation (~11 s)
bench/vbench.py gate                # performance gate: instructions vs the merge-base, budget 1 % (~1 min)
bench/vas-enc.py $PWD/ccs/vcc/tests/vas   # vas encodings vs llvm-mc
bench/pg/build.sh clang && bench/pg/build.sh vcc   # PostgreSQL (macOS, meson+ninja): build + meson test (~2 min each)
bench/pg/build.sh vccd             # ... CC = vcc shim + private vccd (VCC_DELEGATE=bench/pg/cc for the rest)
bench/pg/speed.py                  # PostgreSQL compile speed: clang, clang -O0, vpp, vpp --batch
```
Fast loop while iterating (cold numbers on a loaded machine, in brackets):
`vcc-sema.py batch` (3 s [33 s]), `vcc-diff.py batch --cmp --kernel` (3 s
[14 s]; `cmp` 16 s [40 s]: each reference's token-stream hash is kept in
`work/ppref/X.i.th`, keyed by its size and mtime, so a match lexes only our
side; `VPP_CMP_NOCACHE=1` compares in full), `vcc-ctests.py -j 6` (5 s [32 s]: a vcc object byte-identical to one
that passed is not linked or run again; `--no-cache` runs all) and
`vcc-sema.py layout` (clang only rechecks TUs whose claims changed;
`NOCACHE=1` rechecks all).  The batch modes share caches across TUs as vccd
does, so before a commit also run the per-process `cmp` and `run`.
PostgreSQL (`work/postgres`, `bench/pg/`) is the second test project: a
native macOS build where vcc compiles every C file (`bench/pg/cc`) and clang
links; the reference is Homebrew clang, and both builds must pass the same
`meson test` set.  A bug found there gets a test in `ccs/vcc/tests/vcc/`.
`TODO.md` lists what is left before vcc is a production compiler, each item
with how it is validated.  The vcc suite, stress and vccd tests mark known bugs
`xfail` with their TODO.md item; a fix that makes one pass turns it into an
XPASS error, so the same commit removes the marker.
Parity is a token-stream comparison (kind and spelling) of `clang -E -P`
against `vpp`, both lexed by vcc's lexer.  A change to `pp/` or `lex.c` is
only done when the parity run is fully green; a change to `cc/` when both
vcc-sema.py runs are green (878/879: modpost.c lacks a generated header).

## Sanitizers
```
make -C ccs -j 6 vcc-asan           # -fsanitize=address,undefined -> build/ccs-asan/
make -C ccs -j 6 vcc-tsan           # -fsanitize=thread            -> build/ccs-tsan/
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1   # UB fails the TU
bench/vcc-sema.py run -j 6 --bin-dir build/ccs-asan       # any harness importing vcc-diff.py
bench/vcc-diff.py batch -j 6 --cmp --kernel --bin-dir build/ccs-tsan   # shared header cache, 6 threads
bench/vcc-cbatch.py -j 6 --bin-dir build/ccs-tsan         # -c in one process, as vccd
bench/vccd-cc.py --bin-dir build/ccs-tsan                 # vccd itself, 150 TUs; report in $LOG
```
Both variants are `-O1 -g` with their own `obj/`; the normal build is
unchanged.  `--bin-dir DIR` (or `VCC_BIN=DIR`) picks the tools in every
harness that imports `vcc-diff.py`.  The big reservations (16 GB shared pp
region, 4 GB NORESERVE arenas) need no special handling.  A TSan batch takes
65-100 s (15-20x the normal build), an ASan suite 12-25 s.  Limits: ASan does
not see over-reads inside our own mmap'ed arenas (they are never poisoned),
only in malloc, globals and the stack.  Contract that ASan checks: strings
handed to `vintern*`/`vlex_get` must be readable 16 (or 8) bytes past their
length (`vintern_cstr` pads a literal).  No suppressions are needed today;
put any in `ccs/tsan.supp`, one reason per entry, and run with
`TSAN_OPTIONS=suppressions=$PWD/ccs/tsan.supp`.

## Fuzzing (`ccs/fuzz/`)
Targets build with Homebrew LLVM's libFuzzer and ASan+UBSan into
`build/ccs/fuzz/` (without libFuzzer, `driver.c` gives the same command line
minus coverage feedback).
- `fuzz-lex`: vlex on raw bytes; checks token-array invariants, spelling,
  interning, NEON vs a scalar build of lex.c (`lexref.c`), buffer alignment.
- `fuzz-pp`: one in-memory TU served by `vlex_get_hook`: main `\x01` a.h
  `\x01` b.h `\x01` sys/c.h, leading `\x02` for asm mode.
  `FUZZ_PP_PRINT=1` prints output and diagnostics.
```
make -C ccs fuzz                    # build both targets
ccs/fuzz/gen-corpus.py              # seed corpora from work/linux headers -> build/ccs/fuzz/corpus
cd build/ccs/fuzz && ./fuzz-pp -fork=2 -max_total_time=180 -dict=../../../ccs/fuzz/c.dict \
    -max_len=4096 -timeout=5 -artifact_prefix=art/ run-pp corpus/pp   # same for fuzz-lex
make -C ccs fuzz-regress            # replay ccs/fuzz/regress/{lex,pp}
make -C ccs fuzz-diff               # vpp vs clang -E -P on regress + seeds
ccs/fuzz/pp-diff.py -j 4 DIR...     # ... on any corpus; mismatches -> build/ccs/fuzz/diff/
```
Keep runs to minutes with `-fork=2`.  Every fixed finding gets a minimal
input in `regress/lex` or `regress/pp`.

## Working conventions
- Commit after each completed step (small, self-contained commits with a
  message saying what changed and how it was verified).
- Clang is the reference: when behaviour is unclear, test it against clang
  on a small file before implementing.
- Measure before and after performance work; report numbers in the plan.
