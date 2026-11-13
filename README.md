# vroom

**vcc** is a fast C toolchain: preprocessor, compiler, assembler, linker
and archiver, written from scratch.  It runs as a long-lived server (vccd),
so work that translation units share is done once.  It is meant to be used
as a drop-in compiler with `CC=vcc`.

**vroom** is the project around it: a cold arm64 Linux kernel build on
macOS, from an empty tree to a bootable `Image`, with a target of under
2 seconds on an M1 Pro.

## Where it stands

- **The kernel builds without clang, LLVM or binutils.**  Every `.c` and `.S`
  file is compiled by vcc, linked by vcc's linker and archived by vcc's
  `ar`.  The kernel boots Debian to `is-system-running = running`.
- **A cold kernel build takes about 10 s** with vccd alone.  Most of the remaining time is make and Kbuild,
  not the compiler.
- **Preprocessor parity with clang:** `vcc -E` matches `clang -E`
  token for token on the kernel's TUs, and `-dM`, `-M`, `-H`, `-dD` and
  line markers match byte for byte.
- **Real-world projects:** PostgreSQL (all `meson test` passing), SQLite
  (TCL test runner), zlib and ffmpeg (FATE) build with vcc on macOS.
  vcc also compiles itself.
- **Targets:** arm64 Linux (ELF) and arm64 macOS (Mach-O) are the main
  targets.  x86-64 Linux and WebAssembly (wasm64/wasm32) are in progress,
  and there is an in-browser build for a compiler-explorer page.
- **Optimisation:** `-O0`/`-O1` use a fast direct emitter with a peephole
  and register pass.  `-O2` goes through **VIR**, an SSA IR with its own
  register allocator and loop vectorizer ([SPEC-vir.md](doc/SPEC-vir.md)).
- **Diagnostics:** clang-compatible locations and flags are the minimum.
  On top of that, vcc has its own text styles, JSON, SARIF and HTML
  output, `--fix`, and `vcc --explain ID`.

vcc is not at 1.0 yet.  [TODO.md](TODO.md) lists what is left, ordered by
risk, and [PLAN-release.md](doc/PLAN-release.md) defines the release gates G0-G9.

## How it is fast

```
make ──► vcc (shim, ~100 µs) ──unix socket + SCM_RIGHTS──► vccd (one process, thread pool)
             │                                               ├── header cache: tokens, macros, declarations
             └── vas / vld / var / vsh shims the same way    ├── TU job: lex → pp → parse → sema → emit
                                                             ├── built-in assembler (.S and inline asm)
                                                             └── built-in linker: objects in memory → vmlinux → Image
```

- **One server, many TUs.**  The kernel's ~840 TUs each read about 880 headers,
  4-9 MB of source per TU, but there are only 44 MB of unique text.  vccd
  lexes, preprocesses and parses shared headers roughly once and replays them.
- **Lazy function bodies.**  Header function bodies are saved as tokens and
  parsed only when used.  About 2 % ever are.
- **SIMD everywhere it pays:** NEON lexer, mmap'd input, arenas instead of
  malloc, separate I/O and CPU threads.
- **No text in the middle.**  The emitter writes objects directly.  Assembly
  text is produced only for `-S`.
- **Kbuild stays.**  make is not replaced.  A few small kernel patches
  ([patches/](patches/)) remove serial steps before the first compile, and
  `vsh` runs Kbuild's recipe subset of sh inside vccd.

## Repository layout

| Path | What |
|---|---|
| [src/](src/) | the toolchain: `frontend/lex/`, `frontend/pp/`, `frontend/c/` (parser + sema), `codegen/`, `vir/`, `as/`, `obj/`, `ld/`, `driver/`, `vccd/`, `lib/` |
| `src/arch/<cpu>/` | per-CPU code: `a64`, `x64`, `wasm` |
| `src/platform/<os>/` | per-OS object formats: `linux` (ELF), `darwin` (Mach-O), `wasm` |
| [src/test/](src/test/) | test suites (own, plus vendored gcc c-torture, tcc tests2 and others) |
| [src/test/fuzz/](src/test/fuzz/) | libFuzzer targets for the lexer, preprocessor and diagnostics |
| [contrib/wasm/](contrib/wasm/) | vcc built as WebAssembly for the browser |
| [contrib/phase1/](contrib/phase1/) | phase 1's clang-based tools (`vroomd`, `vroom-cc`) |
| [bench/](bench/) | harnesses: parity against clang, test runners, benchmarks, kernel build and boot |
| [ci/](ci/), [env/](env/) | CI scripts, kernel configs and the Linux/macOS environments |
| [patches/](patches/) | kernel build patches |
| [doc/](doc/) | plans, specs, research notes and reviews |

[AGENTS.md](AGENTS.md) has the full per-file map.

## Building

You need macOS on arm64 or Linux (arm64 or x86-64) and a C compiler to
bootstrap with (clang).  At run time vcc does not depend on clang.  Tables
taken from clang once (capabilities, predefined macros, `__has_*` answers)
are generated files in the tree.

```bash
make -C src -j 6 vcc
```

The tools are written to `build/src/`: `vpp` (one compile job, the test
driver), `vccd` and its `vcc`/`vas`/`vld`/`var`/`vsh` shims, `vlink`,
`vasm`, `vnm`, `vobjcopy`, `virtest`.

Other variants:

```bash
make -C src -j 6 vcc-asan     # ASan + UBSan -> build/src-asan/
```

```bash
make -C src -j 6 vcc-tsan     # TSan -> build/src-tsan/
```

```bash
make -C src -j 6 vcc VCC_UNITY= OUT=../build/src-parts   # file by file instead of one TU per module
```

## Using it

As a drop-in compiler for a project (macOS example, PostgreSQL):

```bash
bench/pg/build.sh vcc
```

A cold kernel build with vccd only (needs the kernel tree in `work/linux`,
see `env/`):

```bash
bench/vccd-kbuild.sh work/build-X -j 6
```

```bash
python3 bench/boot-test.py --kernel work/build-X/arch/arm64/boot/Image --timeout 60
```

## Testing

Clang is the reference.  Most harnesses compare vcc with clang directly,
whether that is token streams, objects, layouts, diagnostics or program
output.  A few that need only the checkout:

```bash
bench/vcc-ctests.py -j 6          # C suites: built by clang and vcc, run under qemu, output compared
```

```bash
bench/vcc-abi.py                  # cross-compiler calls, clang/vcc x caller/callee
```

```bash
bench/vcc-diag.py -j 6            # diagnostics snapshots and clang's locations/levels/flags
```

```bash
bench/vir-test.py -j 6            # VIR parser, printer, verifier, dominators
```

```bash
bench/vbench.py gate              # performance gate: instructions vs merge-base, 1 % budget
```

The kernel parity runs (`bench/vcc-diff.py`, `bench/vcc-sema.py`) need a
kernel tree under `work/`.  CI runs `ci/premerge.sh` per target. To run it
locally the same way:

```bash
env/linux/run.sh ci/premerge.sh arm64
```

[AGENTS.md](AGENTS.md) lists every harness, the sanitizer and fuzzing
setup, and the working conventions.

## Documentation

Plans and status notes are written in Swedish; the specs, research notes
and agent notes are in English.

- [PLAN.md](doc/PLAN.md), [PLAN-fas2.md](doc/PLAN-fas2.md): phase 1 (measurement, clang-based) and phase 2 (own toolchain), with measured status per step
- [PLAN-release.md](doc/PLAN-release.md): what vcc 1.0 is and its release gates
- [PLAN-vir.md](doc/PLAN-vir.md), [SPEC-vir.md](doc/SPEC-vir.md): the SSA IR used at `-O2`
- [PLAN-x86.md](doc/PLAN-x86.md), [PLAN-wasm.md](doc/PLAN-wasm.md), [PLAN-diag.md](doc/PLAN-diag.md): further targets and diagnostics
- `doc/RESEARCH-*.md`: preprocessor, VIR, register allocation, vectors, diagnostics
- [TEST.plan](doc/TEST.plan): external C test suites and how vcc is validated against them
- [TODO.md](TODO.md): what is left before a production compiler, each item with its validation

## License

vroom and vcc are released under the [MIT License](LICENSE).

The vendored test suites under `src/test/` keep their own licenses
(for example GPL-3.0 for gcc c-torture and LGPL-2.1 for tcc tests2).
[src/test/README.md](src/test/README.md) lists each suite with
its source and license.  The kernel patches in [patches/](patches/) apply
to Linux and fall under its license (GPL-2.0).
