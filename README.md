<h1 align="center"><a href="doc/logos/hairpin-cartoon.png"><img src="doc/logos/hairpin-cartoon.png" alt="vroom" width="420"></a></h1>

**vroom is a C toolchain built to get from a clean kernel tree to a bootable arm64 Linux image in under two seconds on an M1 Pro.** The goal is still ahead of us. A recorded cold build took 9.9 seconds, and the resulting kernel booted Debian.

The compiler is called `vcc`. Set `CC=vcc` and make sends jobs to `vccd`, a long-lived process that compiles C, assembles code, builds archives and links the kernel. Clang bootstraps the tools. The kernel build itself runs entirely on vroom.

## ⏱️ Performance

| Workload | Recorded time | What was timed |
| --- | ---: | --- |
| Cold arm64 kernel build on M1 Pro | **9.9 s** | 884 compile jobs. Wall time was 11.1 s including harness startup and shutdown. [Build notes](doc/PLAN-fas2.md) · [Debian boot log](results/boot-20261110-133025.log) |
| Compile SQLite to an x86-64 object | **292 ms** direct · **571 ms** via VIR | 262,899 lines in one C file on a Ryzen 7 9700X running Linux. Median CPU time over three fresh compiler processes with the file cache retained. [Measurements](results/sqlite-compile-20261113.json) |
| Tokenize the same SQLite source | **4.9 ms** | 9.3 MB and 1.14 million tokens on the Ryzen. Best of seven warm lexer passes, or 1.91 GB/s. [Lexer results](results/lexer-simd-20261113.json) |

About 840 kernel translation units each pull in 4–9 MB of source, but together they read just 44 MB of distinct text. Keeping tokens, macros and parsed declarations in one process lets the next compile reuse a header the previous one already read. Header functions stay as tokens until something uses them. Only about 2% need parsing.

The emitter writes object files directly. Kbuild still schedules the work, and a small shell inside `vccd` handles its common recipes. Several [kernel patches](patches/) shorten the serial part before the first compile.

## 🔧 Build and try it

Build with clang on Apple Silicon or Linux (arm64 or x86-64). The tools land in `build/src/`. To try the standalone driver on one file:

```sh
make -C src -j 6 vcc
echo 'int square(int x) { return x * x; }' > /tmp/vroom-square.c
build/src/vpp --target=aarch64-linux-gnu -c /tmp/vroom-square.c -o /tmp/vroom-square.o
```

For the kernel benchmark, set up the kernel tree under `work/linux` using the [build environment](env/), then build and boot the image:

```sh
bench/vccd-kbuild.sh work/build-X -j 6
python3 bench/boot-test.py --kernel work/build-X/arch/arm64/boot/Image --timeout 60
```

## Compiler status

The main targets are arm64 Linux (ELF) and macOS (Mach-O). The x86-64 Linux and WebAssembly backends are in development. There's also a [browser build of the compiler](contrib/wasm/).

- **Code generation.** At `-O0` and `-O1`, vcc emits machine code directly. At `-O2` it can use VIR, an SSA intermediate representation with a register allocator and loop vectorizer. [VIR specification](doc/SPEC-vir.md)
- **Compatibility.** The preprocessor matches clang token for token on kernel translation units. Macro dumps, dependency lists, include traces and line markers have byte-for-byte checks.
- **Programs that build.** PostgreSQL, SQLite, zlib and FFmpeg build with vcc on macOS. vcc also compiles itself. To try the PostgreSQL build, run `bench/pg/build.sh vcc`.
- **Diagnostics.** vcc supports clang-compatible locations and flags, JSON, SARIF and HTML output, plus `--fix` and `--explain ID`.

## Tests

```sh
bench/vcc-ctests.py -j 6     # Compare program output with clang under QEMU
bench/vcc-abi.py             # Mix callers and callees from both compilers
bench/vcc-diag.py -j 6       # Check diagnostic text, locations and flags
bench/vir-test.py -j 6       # Check the IR parser, printer and verifier
bench/vbench.py gate         # Compare instructions against the merge base (1% budget)
```

Run the arm64 CI suite locally with `env/linux/run.sh ci/premerge.sh arm64`. For sanitizer builds, use `make -C src -j 6 vcc-asan` or `vcc-tsan`. The [contributor notes](AGENTS.md) cover file-by-file builds, fuzzing and the kernel parity tests.

## Repository layout

| Path | What |
|---|---|
| [src/](src/) | the toolchain, laid out after PostgreSQL's tree ([doc/PLAN-tree.md](doc/PLAN-tree.md)) |
| `src/frontend/` | the language side: `lex/`, `pp/`, `diag/`, `c/` (parser + sema) |
| `src/codegen/`, `src/vir/` | code generation from the AST, and VIR, the SSA IR of `-O2` |
| `src/as/`, `src/obj/`, `src/ld/` | assembler, in-memory object, linker |
| `src/driver/`, `src/server/`, `src/utils/` | compile jobs, the `vccd` server, the kbuild helpers it runs (`ar`, `sh`, fixdep) |
| `src/bin/<prog>/` | each program's `main`: `vcc` (shim), `vpp`, `vasm`, `lextest`, `virtest`, `viralloc` |
| `src/include/` | the headers modules share, at their module's path (`common/arena.h`, `frontend/pp/pp.h`) |
| `src/common/`, `src/port/` | shared data structures; host-specific code |
| `src/headers/` | vcc's own `<arm_neon.h>`, `<stdatomic.h>`, ... |
| `src/tools/` | generators of checked-in source (`gen-*`) |
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

## Documentation

All documentation (plans, status notes, specs, research notes and agent
notes) is in English.

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
