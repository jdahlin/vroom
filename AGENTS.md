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
  - `vcc/pp.c`: preprocessor (pull model, clang-exact expansion).
  - `vcc/cc.c`: parser + sema (GNU C11, clang's layout and constant rules).
  - `vcc/vpp.c`: preprocessor driver for parity tests.
  - `vcc/shim.c`, `vccd.c`, `caps.c`: the compiler shim and server.
- `bench/`: harnesses.  `bench/vcc-diff.py` compares `vpp` with `clang -E`;
  `bench/vcc-sema.py` runs the front end and has clang verify its claims.
- `work/` (git-ignored, case-sensitive sparsebundle): kernel source in
  `work/linux`, build tree in `work/build-o0` (TU command lines in
  `**/.*.o.cmd`), clang reference outputs in `work/ppref/`.

## Build and test
```
make -C ccs -j 6 vcc                # all vcc tools (per-file objects, ccache if installed)
bench/vcc-diff.py ref -j 6          # clang -E references (once, ~1 min)
bench/vcc-diff.py cmp -j 6 --kernel # preprocessor parity, kernel TUs
bench/vcc-diff.py cmp -j 6          # ... including host tools (scripts/)
bench/vcc-sema.py run -j 6          # vpp -fsyntax-only on every C TU
bench/vcc-sema.py layout -j 6       # layouts/constants/decl types checked by clang (~70 s)
```
Parity is a token-stream comparison (kind and spelling) of `clang -E -P`
against `vpp`, both lexed by vcc's lexer.  A change to `pp.c` or `lex.c` is
only done when the parity run is fully green; a change to `cc.c` when both
vcc-sema.py runs are green (878/879: modpost.c lacks a generated header).

## Working conventions
- Commit after each completed step (small, self-contained commits with a
  message saying what changed and how it was verified).
- Clang is the reference: when behaviour is unclear, test it against clang
  on a small file before implementing.
- Measure before and after performance work; report numbers in the plan.
