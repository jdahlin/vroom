# vroom: agent notes

Fast cold arm64 Linux kernel build on macOS (M1 Pro: 8 P + 2 E cores) that
boots Debian.  Phase 1 used clang; phase 2 (`PLAN-fas2.md`) replaces it with
our own toolchain, `src/`: lexer, preprocessor, parser, emitter,
assembler, linker, behind `vcc`/`vas`/`vld` shims and the `vccd` server.

## Goals and constraints (from the user)
- Cold build under 2 s; every part state of the art in performance (SIMD,
  parallel, minimal allocation, mmap, separate I/O and CPU threads).
- No clang dependency at run time.  Data taken from clang once (capability
  tables, predefined macros, `__has_*` answers) lives in generated files
  (`src/server/capabilities.c`, `src/frontend/pp/ppdata*.inc` via `gen-ppdata.sh`).
- Do not replace make.
- The machine is shared: keep benchmarks short and use `-j 6` for
  parallel diff runs.

## Layout
Plans, specs, research notes and reviews are in `doc/` (names below are
relative to it); `TODO.md` stays at the top.
- `PLAN.md`, `PLAN-fas2.md`, `PLAN-wasm.md` (WebAssembly as a target), `PLAN-x86.md`
  (x86-64 Linux as a target), `PLAN-win64.md` (Windows x64 through MinGW-w64,
  steps WIN0-WIN10): plans
  and per-step status.  Update the
  status section of a step when it changes.
- `PLAN-quality.md`: the code quality work before C++ (duplication, one
  scheduler, one pass per file, names): method, stages P0-P4, baseline.
- `PLAN-tree.md`: reorganising the source tree PostgreSQL-style (`src/`,
  `src/include/`, `frontend/`, `bin/`, ...) before a C++ front end.
- `PLAN-release.md`: what vcc 1.0 is and its release gates G0-G9;
  work toward the release should move a gate, and update its status there.
- `RESEARCH-preprocessor.md`: preprocessor design and prior art.
- `REVIEW-vir-lowering.md`: review of the VIR builder, format and arm64
  lowering (2026-11-12): bugs fixed and open, compile-time and runtime
  findings, spec drift, decisions needed.
- `PLAN-vir.md` (steps V0-V7), `SPEC-vir.md` (VIR format, draft) and
  `RESEARCH-vir.md` (LLVM IR's problems): VIR, an SSA IR used only at `-O2`,
  built by compiling the shared walker a second time against a virtual
  lowering target.  Update the status of a step in `PLAN-vir.md`.
- `src/codegen/vir/`: the VIR builder, one pass per file (`preir.h` the pre-IR and what the files share, `ssa-construction.c`, `lower-*.c`, `slot-promotion.c`, the simplifier in `rules.c`, `value-numbering.c`, `load-forwarding.c` and `simplify.c`, `if-conversion.c`, `condition-merge.c`, `to-vir.c`, `pipeline.c`).  `vectorize.c` is the loop vectorizer, on
  the builder's pre-IR; `VCC_VIR_VEC=0` turns it off, `r` reports each
  innermost loop to stderr, `n` keeps loop constants unhoisted (`virbuild_loop_constants_pin`).  VIR's vectors: `SPEC-vir.md` 7.5.
- `src/arch/a64/vir/` (internal header `lowering-internal.h`; files by topic: `calling-convention.c`, `moves.c`, `compares.c`, `memory.c`, `calls.c`, `switch.c`, `address-folds.c`, `returns-and-jumps.c`, `vectors.c`, `instructions.c`, `function.c`) and `src/arch/x64/vir/` (the same cut): VIR's arm64 and x86-64 lowerings.  arm64: `VCC_VIR_ROT=0` keeps VIR's block order (no loop
  rotation), `VCC_VIR_SHRINK=0` the prologue at the function's entry (no shrink-wrapping); `VCC_VL_PROBE=MASK` puts a nop after each allocator edit of the kinds in MASK, so that
  instructions retired count how often that kind runs.
- `RESEARCH-vectors.md`: vector values in VIR (how Cranelift, Wasm SIMD, B3,
  LLVM and GCC represent them; ABI and spill slots), loop vectorizers (LLVM,
  GCC, dependence rules, TSVC results here and published), the traps, and the
  proposal: lane-typed 128-bit types, a first loop vectorizer, prerequisites.
- `RESEARCH-regalloc.md`: register allocation (GCC, LLVM, SSA-based, linear
  scan and its descendants, optimal and learned methods) and the allocator
  `SPEC-vir.md` section 15 specifies for VIR, with its fallback.
- `RESEARCH-cplusplus.md`: whether vroom's architecture (vccd, prefix
  snapshots, direct -O0 emitter) carries to C++, prior compile-server work on
  the LLVM/GCC/kernel lists, and the inputs where it stops paying.
- `src/`: the toolchain; `src/Makefile` builds into `build/src/`.  The tree
  follows PostgreSQL's (`PLAN-tree.md`): generic code in the component
  directories, code for one target CPU in `arch/<cpu>/<component>/`, code for
  one target OS in `platform/<os>/` (target, not host: host-specific code is
  in `port/`, host SIMD `#ifdef`s stay with their code).
  - `include/`: the headers another module includes, at the path of the
    module (`include/common/region.h`, `include/frontend/pp/pp.h`,
    `include/arch/a64/a64.h`, `include/protocol.h`); code in `src/` includes
    them by that name with `-Isrc/include`.  A header only its own module uses
    stays next to its files, and a module's files include each other by
    relative path.
  - `common/`: the shared library, portable C (headers in `include/common/`;
    `include/common/common.h` is its overview and invariants; unit tests in
    `test/common/`, `bench/common-test.py`).  One failure path (`fail.h`:
    `vjob_fail`, `vout_of_memory`, `VBUG`); regions (`region.h`), the byte
    buffer `vbuffer` (`buffer.h`) and the emitter's text buffer
    (`text-buffer.h`), growable arrays (`vector.h`), `string-index.h` and the
    string hash (`hash.h`), the identifier table (`intern.c`), `bytes.h`,
    `leb128.h`, `format.h`, the ar archive reader (`archive-reader.c`), digests (`digest-xxh3.c`, `digest-blake3.c`,
    `digest-sha256.c`), binary128 and x87 soft float (`softfloat.h`),
    `bigint.h`, scoped resources through `cleanup` (`scoped-cleanup.h`:
    VAUTOFREE, VAUTOCLOSE, VLOCK; not across a longjmp), attributes
    (`attributes.h`: VUSE, VNONNULL) and overflow-checked bounds for sizes
    read from input files (`checked-arithmetic.h`).  `common_SRC` in the
    Makefile; every tool links it.
  - `port/`: what depends on the host (headers in `include/port/`; `port_SRC`,
    linked by every tool): job regions kept from one job to the next
    (`job-regions.h`), file mapping, reading and writing (`file.h`), clocks
    (`clock.h`), thread counters (`performance-counters.h`), the stack guard
    (`stack-guard.h`), CRC32C (`crc32c.h`), `-march=native` on x86
    (`x86native.h`), the shared thread pool and CPU budget (`task.h`,
    `vtask_*`: parallel-for, jobs, blocking regions;
    doc/quality/WORK-scheduler.md; `-j`/`VCC_JOBS`), Apple PMU counters
    (`darwin/pmc.c`), and the wasm build's `wasm/mman.c` (the big
    reservations as small sbrk stretches) and `wasm/compat/` (ACLE CRC
    through SIMDe).
  - `headers/`: vcc's own `<arm_neon.h>`, `<arm_acle.h>`, `<arm_sve.h>`,
    `<stdatomic.h>`, the compiled program's resource headers (`VCC_INCDIR`).
  - `frontend/`: the language side, from bytes to a typed AST.
    - `lex/`: SIMD lexer and token cache (`bin/lextest/` drives it).
    - `pp/`: preprocessor (pull model, clang-exact expansion), its generated
      tables (`ppdata*.inc` via `tools/gen-ppdata.sh`) and the
      include-directory cache.
    - `diag/`: the diagnostics engine (records, text styles, JSON, SARIF,
      HTML; ids in `diagids.inc`).  Compiled in the pp module for now: it
      uses `pp-internal.h`.
    - `c/`: parser + sema (GNU C11, clang's layout and constant rules).  C++
      and Objective-C go next to it.
  - `codegen/`: code generator (straight from the AST): the shared part, state
    and types (`codegen-internal.h`), the work queue and threads (`unit-emit.c`), data
    objects, the prepass; the arm64 part is `arch/a64/codegen/`.
  - `as/`: GNU as syntax (`as.c`: statements, directives, macros,
    conditionals, expressions; internal header `as-internal.h`; one module with
    `arch/<cpu>/as/parse.c`, `as_SRC`).
  - `obj/`: the in-memory object (`obj.h`: sections, frags, symbols, fixups)
    and its layout; with `arch/<cpu>/obj/fixup.c` and `platform/linux/elf.c`
    one module (`obj_SRC`, internal header `objimpl.h`).
  - `vir/`: VIR, the SSA IR of `-O2` (SPEC-vir.md; one module `vir_SRC`, a unity
    TU like `as`): `vir.h` the public format
    (instruction, block, slot, function, the opcode table `opcode-table.c` that the
    printer, parser and verifier all read), `vir.c` the thread's reservations
    and construction (hash-consed constants, `extra` lists), `vir_cfg_build`,
    `vir_dominators_build`, `vir_uses_count`, `vir_canon`, and the unit tables of the text form;
    `print.c`, `parse.c` (arbitrary bytes fail as `file:line:col`, never crash)
    and `verify.c` (every rule of SPEC section 12); `vir-internal.h` is internal.
    Passes on VIR: `include/vir/pass.h` (interface, registry, `vir_pass_run`'s check of
    `keeps`), `pass-list.c` (the registry), `compact.c` (`vir_function_compact`), one pass
    per file in `pass/`; `virtest --passes` runs them (`doc/quality/DESIGN-vir-optimizer.md`).
    Depends on nothing in `frontend/`, `codegen/` or `arch/`.
    `bin/virtest/` is the tool
    (`virtest FILE`; `--no-verify`, `--bless`, `--selftest` for the builder API).
    Tests: `test/vir/` (`bad/RULE-name.vir`, `canon/`, `dom/`).
  - `ld/`: the linker `vlink` (`ld.c` includes its parts, files by topic
    sharing `ld-internal.h`, as one TU, a split module like `as`; `ld_SRC`,
    main in `vlink-main.c`; also linked into vccd, which calls `ld_main` per
    link, each link with its own `struct ld_context`) and `vobjcopy`/`vnm`;
    `wasmld.c` is vlink for WebAssembly (as `wasm-ld` or with `-mwasm64`).
  - `driver/`: one compile job from a clang command line (`job.c` calls the
    pieces: `cmdline.c`, `searchpath.c`, `predef.c`, `depfile.c`, `output.c`, ...;
    `driver-internal.h`), the declaration cache (`declaration-cache.c`), `--explain` pages (`explain.inc`).
  - `server/`: vccd (`vccd.c`, with its main; one file per job beside it,
    `vccd-internal.h`), the diagnostics store
    (`diagnostics-store.c`), the capability table (`capabilities.c`) and the warning groups
    (`warnings.inc`).  Without `--backend-sock` vccd runs the whole kernel
    build itself: probes, `-E`/`-c`/`-S` (stdin too), links (vld in-process;
    `--ld PROG` spawns one), nm/objcopy behind the `vcc-nm`/`vcc-objcopy`
    shims, and the host programs (vcc + `macho-linker.c`, macOS only); nothing
    falls back to clang.
  - `utils/`: what runs inside vccd for kbuild: fixdep, the archiver (`ar.c`:
    thin archives, llvm-ar-identical, so the build uses no llvm-ar) and
    `shell.c`, kbuild's recipe subset of sh, run for `vsh`
    (`build/src/vsh/sh`, the shim with `-DVSH`), make's SHELL in
    `bench/vccd-kbuild.sh`; anything outside the subset goes to `$VSH_REAL`.
  - `bin/<prog>/`: one directory per program's main: `vcc/shim.c` (the
    `vcc`/`vas`/`vld`/`var`/`vsh` shim), `vpp/` (the driver for parity
    tests), `vasm/`, `lextest/` (with `lexdiff.py`, `lexpmc.sh`), `virtest/`,
    `viralloc/`.
  - `arch/a64/`: arm64: instructions in GNU as syntax (`as/parse.c`:
    registers, `.req`, operands, literal pools), the encoder (`as/enc.c`,
    `include/arch/a64/a64.h`), system registers (`a64sysreg.inc`),
    relocation types (`include/arch/a64/reloc.h`), fixups (`obj/fixup.c`: in place or a
    relocation, as LLVM MC), the linker's relocation scan, relocation
    application and range-extension thunks (`ld/*.c`, parts of the
    linker's module);
    `codegen/`: instruction records and register names (`ins.h`, `a64impl.h`,
    included by `codegen/codegen-internal.h`), expressions, statements, calls (AAPCS64
    and Apple's), builtins, inline asm, the record optimizer (`opt.c`), the
    printer (`print.c`) and the encoder of records (`obj.c`).  It is compiled
    in the `codegen` unity TU (`codegen_SRC`), so VPRIV stays static.
  - `codegen/walk/` and `codegen/rec/`: the shared AST walker (`walk.h`: folding,
    frame slots, labels, the call sequence; it calls a CPU only through
    `LO()`) and the machine records with their per-CPU op table (`rec.h`),
    PLAN-x86.md's layers 1 and 3; `arch/x64/codegen/`: x86-64's lowering
    (`x64impl.h`, `lower.c`), two-address pass and AT&T printer (`print.c`).
    x86-64 is their only user; arm64 moves to them later (TODO.md).
    `walk/prefix.h` is generated from the walker's definitions: with
    `WK_PREFIX`/`LO_PREFIX` defined the walker compiles a second time under
    other names (`src/tools/gen-walkprefix.py`; PLAN-vir.md).
  - `arch/x64/as/`: the x86-64 assembler, AT&T syntax with MC's encodings:
    operands, the encoder (REX and VEX) and the dispatcher in `asm.c`
    (shared header `x64as.h`), one file per instruction family (`sys.c`,
    `gpr.c`, `x87.c`, `sse.c`, `avx.c`); `vas-obj.py` checks `x86-*.s`.  The
    x86-64 target's feature table is `include/arch/x64/x86features.h`.
  - `arch/wasm/codegen/`: the WebAssembly (wasm64) code generator, AST to
    LLVM's wasm `.s` (`wimpl.h`; `wasm.c` functions, ABI, header; `wexpr.c`,
    `wcall.c`, `wstmt.c`), also in the `codegen` unity TU: every name starts
    with `w`.  `codegen/` picks it per target (`WASM(c)`); PLAN-wasm.md.
  - `arch/wasm/as/`: the wasm assembler (`as.c`, model in `was.h`, interface
    `include/arch/wasm/as/wasmas.h`): LLVM's wasm `.s` to a relocatable
    object; `-c` for wasm64 and `vasm --target=wasm64-...` go through it.
  - `platform/linux/`: the ELF writer (`elf.c`: relocations, string table,
    file layout as LLVM MC).
  - `platform/darwin/`: the Mach-O writer; vld's Mach-O executables
    (`macho-linker.c`: libSystem only, chained fixups, ad-hoc signature, chosen
    when the first input is Mach-O; the kernel's host programs).
  - `platform/wasm/`: the wasm object writer (`wasm.c`: layout, symbol table,
    relocations as LLVM's WasmObjectWriter, byte for byte).
  - `platform/win/`: vlink's PE/COFF linker for MinGW x86-64 (`pe-linker.c`,
    one TU like `ld/ld.c` that #includes `pe-options.c`, `pe-input.c`,
    `pe-resolve.c`, `pe-export.c`, `pe-layout.c`, `pe-write.c`; model in
    `pe-linker-internal.h`, COFF/PE constants in `pe-definitions.h`): chosen
    when the first input is COFF x86-64 or by `-m i386pep`; ld.lld's MinGW
    command line, output byte-identical to lld's (doc/PLAN-win64.md WIN7).
  - `runtime/`: vcc's run-time support library for x86_64-w64-mingw32
    (`libvccrt.a`, WIN7R; `make -C src vccrt` runs `runtime/build.sh`, CC and
    AR as parameters): the helpers generated code calls (`__int128` division
    and shifts, `__int128` <-> float conversions, `__float128` soft float,
    `_Float16`/`__bf16` narrowings, complex `*` and `/`, powi) and
    `___chkstk_ms` (`stack-probe.S`), our own code from the helpers'
    specifications (no compiler-rt or libgcc source); one rounding routine
    (`float-rounding.c`) and one bit-layout module (`float-encoding.c`) for
    every format, internal names `__vccrt_*`, no libc or libm.  Rules that
    keep it buildable by vcc are in `runtime-internal.h`.
  - `frontend/pp/`, `frontend/c/` and `codegen/` (with `arch/<cpu>/codegen/`) are directories of
    files by topic, one internal header each (`pp-internal.h`, `cc-internal.h`, `codegen-internal.h`) with the types, macros
    and prototypes the files share; a shared function is `VPRIV` (static in
    the unity build).  Each such module is compiled as one TU by default
    (`obj/unity/MOD.c` #includes the files in `MOD_SRC`, SQLite style); `make
    VCC_UNITY=` compiles file by file, and every file must compile alone.
  - `test/`: the test inputs (`test/vcc/` our regression tests, `diag/`,
    `vir/`, `abi/`, `vas/`, the vendored `gcc-torture/`, `tcc-tests2/`,
    `c90/`, ...; the runtime in `rt/`) and `test/fuzz/` (see Fuzzing).
  - `tools/`: every script that regenerates checked-in source (`gen-*`), and
    `retarget-paths.py`: the moves of PLAN-tree.md as data, `text` to
    retarget a branch started before them.
- `contrib/phase1/`: the clang-based phase 1 tools (`vroomd`, `vroom-cc`, PCH
  prefix) and `vroom-ld`, the ld.lld wrapper the -O0 kernel build links with.
- WebAssembly is disabled since 2026-11-13 (doc/PLAN-wasm.md): `make VCC_WASM=1`
  builds it, the default build leaves its sources out and a wasm target is an
  error; its harnesses, `contrib/wasm/` and `ci/premerge.sh wasm` are not
  maintained and expected to rot.
- `contrib/wasm/`: [needs VCC_WASM=1; not maintained] vcc (vpp) as WebAssembly for a compiler-explorer page
  (`build.sh`, emscripten memory64 -> `build/wasm/site/`; the host glue is in
  `src/port/wasm/`).
- `bench/`: harnesses, correctness and speed (they import each other;
  PLAN-tree.md T6).  `bench/vcc-diff.py` compares `vpp` with `clang -E`;
  `bench/vcc-sema.py` runs the front end and has clang verify its claims;
  `bench/vccd-cc.py` drives a private vccd with `-c` jobs.
- `work/` (git-ignored, case-sensitive sparsebundle): kernel source in
  `work/linux`, build tree in `work/build-o0` (TU command lines in
  `**/.*.o.cmd`), clang reference outputs in `work/ppref/`.

## Build and test
```
make -C src -j 6 vcc                # all vcc tools (frontend/pp/ frontend/c/ codegen/ one TU each, ccache if installed)
make -C src -j 6 vcc VCC_UNITY= OUT=../build/src-parts   # file by file: each file compiles alone
bench/vcc-diff.py ref -j 6          # clang -E references (once, ~1 min)
bench/vcc-diff.py cmp -j 6 --kernel # preprocessor parity, kernel TUs
bench/vcc-diff.py cmp -j 6          # ... including host tools (scripts/)
bench/vcc-diff.py dm -j 6           # -dM byte for byte vs clang, C TUs (~40 s)
bench/vcc-diff.py deps -j 6         # -M and -MM -MP -MQ byte for byte vs clang (~80 s)
bench/vcc-diff.py hdr -j 6          # -H include tree vs clang (~60 s)
bench/vcc-diff.py dd -j 6           # -dD: the #define/#undef lines in order vs clang (~60 s)
bench/vcc-diff.py text -j 6         # -E -P byte for byte vs the references (spacing, lines)
bench/vcc-diff.py textc -j 6        # the same with -C (comments), clang run alongside (~2 min)
bench/vcc-diff.py lines -j 6        # -E without -P: line markers byte for byte, clang alongside (~2 min)
bench/vcc-sema.py run -j 6          # vpp -fsyntax-only on every C TU
bench/vcc-sema.py batch -j 6        # the same in one vpp --batch process: ~3 s instead of ~35 s
bench/vcc-sema.py layout -j 6       # layouts/constants/decl types checked by clang (~70 s)
bench/var-cmp.py work/build-x       # vcc ar against llvm-ar on an llvm-ar-built tree
bench/var-cmp.py coff -j 4 [-n N]   # ... no kernel tree: COFF (MinGW) and ELF members, generated, import records, N mutants (~15 s)
bench/vsh-test.py                   # vsh/rsh against dash on small recipes
bench/vobjcopy-cmp.sh               # vnm/vobjcopy against llvm-nm/llvm-objcopy on the build's uses
bench/vobjcopy-any.py work/build-X  # ... and with other options on 200 random .o files
bench/vcc-driver.py                 # driver: command lines vs clang (predefines from -O), <1 s
bench/vcc-fpcheck.py -j 6           # no generated FP/SIMD code under -mgeneral-regs-only (~10 s)
bench/vcc-cfi.py -j 6               # -fasynchronous-unwind-tables: .eh_frame per function, .text unchanged (~1 min)
bench/vcc-dwarf.py -j 6             # -g line tables: dwarfdump --verify, .text unchanged, rows vs clang (~2 min)
bench/vlink-bad.py                  # corrupt objects/archives: vlink (ASan) fails cleanly (<1 s)
bench/vlink-opts.py                 # vlink options vs ld.lld: errors and loaded image (<1 s)
bench/vlink-macho.py --base DIR      # vld's Mach-O linker vs a base build: 80 test programs linked, compared, run (macOS, ~1 min)
bench/vlink-pe.py [--run]           # vlink's PE linker vs ld.lld -m i386pep: options, errors, DLL+implib, byte-identical (~15 s; --run: under Wine)
bench/vlink-pe-bad.py               # corrupt COFF objects/archives/import records: vlink (ASan) fails cleanly (~1 min)
build/src/task-test                 # port/task.c: the pool and budget (also under TSan, <1 s)
bench/sched-measure.py batch|emit|link|vccd|kbuild-log   # scheduling measurements A-F of doc/review/quality-scheduling.md
bench/vcc-pic.py                    # GOT and TLS relocations per -fno-pic/-fPIE/-fPIC vs clang (<1 s)
bench/vcc-volatile.py               # volatile loads/stores kept one for one, direct emitter and VIR (test/volatile, <1 s)
bench/common-test.py                # src/common and src/port unit tests (src/test/common), host compiler, ASan+UBSan (~5 s)
bench/vir-test.py -j 6              # VIR: parser/printer round trip, verifier rules, canon, dominators (src/test/vir, ~1 s)
bench/vir-exposure.py               # VIR: generated long, cyclic and multi-slot exposure checks (<1 s)
bench/vir-test.py --mutate 3000 --bin-dir build/src-asan   # random mutants of those units: virtest never crashes (~1 min)
bench/vcc-ctests.py -j 6 --suite vcc # our own regression tests (src/test/vcc), ~1 s
bench/vcc-ctests.py -j 6 --suite inline   # gcc/llvm inlining and IPA run tests (src/test/inline); VCC_VIR=bl for VIR's inliner
bench/vcc-ctests.py -j 6 --cpu cortex-a53   # vcc's binaries on a CPU without LSE (atomics as LL/SC)
VCC_LDCMP=1 bench/vcc-ctests.py -j 6 --no-cache  # also link with vlink: loaded image byte-identical to lld's (VCC_LD=vlink: run vlink's)
                                    # VCC_CFLAGS=-fasynchronous-unwind-tables: extra flags for both compilers (.eh_frame)
bench/vcc-stress.py [--vccd]        # pathological inputs fail cleanly, vccd survives (~2 s)
bench/vcc-abi.py                    # cross-compiler calls, clang/vcc x caller/callee, qemu (src/test/abi, ~3 s)
bench/vcc-abi.py --host             # the same for the Apple arm64 ABI, run natively
bench/vcc-abi.py --target wasm64    # [needs VCC_WASM=1; not maintained] ... for wasm64 (or wasm32; --vlink), emcc + node (~20 s)
bench/vcc-self.py -j 6              # [needs VCC_WASM=1; not maintained] vcc.wasm built by vcc itself vs the emcc build: -S byte for byte (W6, ~40 s)
bench/vcc-darwin-s.py -j 6           # Darwin -S assembled by Apple's as, runs as the direct object (~2 min)
bench/vcc-diag.py -j 6             # test/diag: vcc's .stderr snapshots, clang's locations/levels/flags (~2 s)
bench/vcc-field-uses.py -j 6       # VCC_FIELD_USES: every use of selected struct fields with its bytes, snapshots (src/test/field-uses, <1 s)
bench/vcc-diag.py lint_naming       # vpp --lint (style checks, off elsewhere; frontend/c/naming-lint.c): cases with /* diag-vpp-flags: --lint */
bench/vcc-diag.py --bin-dir build/src-asan   # ... with a sanitizer build (also vcc-mutate.py, VCC_BIN=$PWD/build/src-asan)
bench/vcc-diag.py --bless PATTERN   # ... rewrite the snapshots of a changed message (review the diff)
src/tools/gen-diagids.py [--check]      # diag/diagnostic-ids.inc: each diagnostic format's id (clang's name or vcc_*)
VCC_TRACK_DIAG=1 vpp ... -fdiagnostics-show-id   # each diagnostic's id and where vcc reported it
bench/vcc-mutate.py -j 6 --formats  # ... and every mutant's JSON rebuilds its text (diag-render.py), SARIF checked, HTML page has the text
vpp ... -fdiagnostics-add-output=html:file=F   # a standalone page of the TU's diagnostics, vcc style in colour
bench/vcc-diag-catalog.py           # build/src/diag-catalog.html: every test/diag case, clang/vcc/narrated/JSON side by side
vpp -fdiagnostics-style=vcc|clang|narrated -fdiagnostics-text-art-charset=ascii|unicode   # text styles (vcc default)
bench/sarif-check.py F.sarif        # SARIF structure (+ the official schema once `fetch`ed)
bench/vcc-darwin-s.py -j 6 --clang   # clang's Darwin -S assembled by vpp -c runs as clang's (~2 min)
bench/fetch-testsuites.sh [NAME]    # external suites -> work/testsuites (clang, gcc, c-testsuite, ...; ~30 s)
bench/vcc-verify.py -j 6 [-v] [PAT] # clang's -verify tests (C/, Sema/, ...): vcc vs the expected-* directives (~6 s warm, 2.5 min cold)
bench/vcc-builtins.py [-v]          # clang builtins unknown to vcc, __has_builtin differences (<1 s)
bench/clangdiag.py fetch|stats      # clang's Diagnostic*.td -> work/clang-td/VER/ (once); counts per language
bench/vcc-diagcov.py [-v] [--missing]   # vcc's diagnostics vs clang's table: coverage, gaps (~1 s)
bench/vcc-mutate.py -j 6             # one injected error per valid TU, vcc vs clang: first error, follow-ons (~25 s warm)
bench/vcc-mutate.py show firstloc     # ... the mutants of one category with both compilers' errors
bench/vcc-mutate.py -j 6 --fix       # ... vcc --fix on every rejected mutant: never worse for clang
bench/vcc-explain.py                # doc/diag pages: the wrong example gives the id, the fixed one nothing
src/tools/gen-explain.py [--check]      # driver/explain.inc from doc/diag/*.md (vcc --explain ID)
src/tools/gen-walkprefix.py [--check]   # codegen/walk/prefix.h: the walker's function names, WK_PREFIX compiles it under another name (VIR)
build/src/viralloc --sweep F.vir     # VIR register allocation (vir/register-allocation.c) on a synthetic target: dump + counters, checker at every register count
bench/vir-alloc.py [--bless]        # src/test/vir-alloc: expected dumps and sweeps (<1 s); `random -n N`: generated functions (~1 s/unit)
bench/vir-alloc.py mutate|time      # the checker catches corrupted allocations; ns per VIR instruction (budget 200 for now, target 60: SPEC 22.1)
bench/vir-failures.py [-n N]        # why VIR gives up on functions (VCC_VIR_STATS=1; reasons: codegen/vir/failure.h): suites, kernel, PostgreSQL, SQLite, most frequent first (~1 min)
bench/record-passes.py -j 3 [-n 200]  # arm64 record clean-ups: records each pass changes, default path vs VCC_VIR=bl (VCC_REC_STATS=1); suites, kernel sample, SQLite (~1 min)
bench/vir-passes.py -j 3 [-n 200]   # VIR builder: runs, changed, instructions and share of the build per pass (VCC_VIR_PASSES=time: codegen/vir/pass-table.c; -NAME, only=, verify); --bisect PAT leaves passes out one by one
bench/vcc-bflayout.py               # generated bit-field records, layouts checked by clang, Linux + Apple (~1 s)
bench/vcc-ldconst.py                # generated long double literals and folds, bits vs clang (~2 s)
bench/vcc-f80.sh [N]                # x87 long double folding (include/common/softfloat.h) vs this x86-64 host's FPU (~3 s)
bench/vccd-test.py                  # vccd as a service: socket, bad requests, cache invalidation (~11 s)
vcc --vccd-diagnostics[=json]       # vccd's diagnostics store: summary by flag/file, or every TU's records (JSON lines)
VCC_DIAG_DEDUP=1 make ...           # under vccd: a header warning another TU of the build printed is left out
bench/vccd-kbuild.sh work/build-X -j 6   # cold kernel build, vccd only (no clang/vroomd/lld; ~10 s)
python3 bench/boot-test.py --kernel work/build-X/arch/arm64/boot/Image --timeout 60   # boots Debian
bench/vbench.py gate                # performance gate: instructions vs the merge-base, budget 1 % (~1 min)
bench/quality.py [--clones N]       # source quality numbers: duplication, private copies of what common/ offers; common/ and port/ gated apart, threads, state, module includes (PLAN-quality.md, 3 s)
bench/quality.py --check            # ... none above bench/quality-baseline.json (--bless rewrites it)
bench/vcc-wasm.py -j 6              # [needs VCC_WASM=1; not maintained] the C suites for wasm64: vcc -S, clang -c, emcc, node vs clang's build (~2 min)
bench/vcc-x86.py -j 6               # the C suites for x86-64 on an x86-64 host: vcc -S, clang -c, glibc, native (~10 s; first run ~10 min for the references)
bench/fetch-testsuites.sh llvm-mingw # the pinned MinGW-w64 sysroot (UCRT) -> work/llvm-mingw (SHA-256 checked; no-op when there)
bench/vcc-win64.py -j 6 --ref-only  # the C suites for x86_64-w64-mingw32: clang + lld + llvm-mingw sysroot, run under Wine (~60 s cold, <1 s cached)
bench/vcc-win64.py -j 6 [--obj|--objcmp]   # ... and vcc's side (vpp -S, clang -c, lld, Wine) vs those references (doc/PLAN-win64.md)
bench/vcc-win64.py -j 4 --vlink-ref # WIN7: clang's objects linked by lld and by vlink (same line): runs under Wine, file byte-identical (~2 min cold)
bench/vcc-win64.py -j 4 --vccrt-ref # WIN7R: clang's objects linked with libvccrt.a instead of compiler-rt, by lld and by vlink, run under Wine; no compiler-rt/libgcc/libunwind input in the map
bench/vccrt-test.py needs -j 4      # WIN7R: helpers the target needs (test objects -O0/-O2, the CRT, vcc's codegen) vs what libvccrt.a defines (<2 s cached)
bench/vccrt-test.py [-n N] [HELPER] # every helper of libvccrt.a vs compiler-rt under Wine, bit for bit on generated inputs (~2 s; -n 300000: 17 M calls, ~20 s)
bench/vccrt-test.py chkstk          # ___chkstk_ms: registers kept, every page written (write watch), big frames on a 1 MiB thread, under Wine (<1 s)
bench/vccrt-test.py native          # the same C sources for x86-64 Linux vs the host's compiler-rt and libgcc (known differences listed) (<1 s)
bench/vcc-wasm.py -j 6 --objcmp     # [VCC_WASM=1] ... and vpp -c's object byte-identical to clang's for the same .s
bench/vcc-wasm.py -j 6 --obj --vlink  # [VCC_WASM=1] ... vcc's side through vpp -c and vlink as emcc's wasm-ld (~9 min)
bench/vcc-same.py --base DIR -j 6    # test-suite outputs byte-identical with DIR/vpp (vbench.py mkbase REV), ~80 s
python3 bench/virgen-test.py --bin-dir build/linux/src  # generated VIR integer/control-flow units verify and round-trip
bench/vas-enc.py $PWD/src/test/vas   # vas encodings vs llvm-mc
bench/vas-obj.py                    # vasm objects vs clang -c byte for byte (src/test/vas-obj, <1 s)
bench/vas-corpus.py -j 6 [-O2] [PAT] # clang -S of the test C files: clang -c vs vasm (~6 min for -O0 -O2)
bench/pg/build.sh clang && bench/pg/build.sh vcc   # PostgreSQL (macOS, meson+ninja): build + meson test (~2 min each)
bench/pg/build.sh vccd             # ... CC = vcc shim + private vccd (VCC_DELEGATE=bench/pg/cc for the rest)
                                   # build: "warnings: N, not from clang: M" (M must be 0; logs in work/build-pg-*.build.log)
bench/pg/speed.py                  # PostgreSQL compile speed: clang, clang -O0, vpp, vpp --batch
bench/sqlite/run.py all            # SQLite (macOS): vcc/clang/gcc builds, TCL testrunner (no vcc-only fails), speedtest1 (~4 min)
bench/zlib/run.py all              # zlib 1.4 (macOS): clang/vcc/vir/gcc builds, self-tests, same bytes as clang's, zbench CPU time (~30 s)
bench/ffmpeg/run.py all            # ffmpeg n8.1.3 without assembly (macOS): clang/vcc/vir builds, FATE (2740 tests, no samples), compile time, codec/filter CPU time (~15 min)
bench/vcc-tsvc.py [-v] [--why]      # TSVC_2 (macOS; fetch-testsuites.sh TSVC_2): 151 loop checksums vs clang -O3, direct emitter and VIR, loops vectorized (~15 s)
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
`meson test` set.  A bug found there gets a test in `src/test/vcc/`.
`TODO.md` lists what is left before vcc is a production compiler, each item
with how it is validated.  The vcc suite, stress and vccd tests mark known bugs
`xfail` with their TODO.md item; a fix that makes one pass turns it into an
XPASS error, so the same commit removes the marker.
Parity is a token-stream comparison (kind and spelling) of `clang -E -P`
against `vpp`, both lexed by vcc's lexer.  A change to `frontend/pp/` or `lex.c` is
only done when the parity run is fully green; a change to `frontend/c/` when both
vcc-sema.py runs are green (878/879: modpost.c lacks a generated header).

## CI and the premerge gate
`.github/workflows/premerge.yml` runs one reusable workflow per target in
parallel (`arm64.yml`: jobs arm64, asan, tsan; x86-64 gets its own; `wasm.yml` is
manual-only, not in the gate), each on GitHub's ubuntu-24.04-arm (tsan on
x86-64's ubuntu-24.04 for now) in a Debian 13 container
(`ci/setup-debian.sh`: clang 23 from apt.llvm.org, qemu; `wasm` adds emsdk).
The test set is `ci/premerge.sh JOB`: the vendored C suites under qemu and
the fast harnesses, no kernel, PostgreSQL or anything needing `work/`.
Locally, as CI: `env/linux/run.sh ci/premerge.sh arm64`.  main is not
protected (private repo, free plan), so the gate is the workflow: push to
main through `ci/premerge-push [--watch]` (pushes to `premerge`; job `gate`
fast-forwards main when everything is green).  A new harness belongs in
`ci/premerge.sh` when it is fast and needs only the checkout.  The asan job also
builds the fuzz targets and replays `test/fuzz/regress` (`make -C src fuzz-regress`).

## Sanitizers
```
make -C src -j 6 vcc-asan           # -fsanitize=address,undefined -> build/src-asan/
make -C src -j 6 vcc-tsan           # -fsanitize=thread            -> build/src-tsan/
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1   # UB fails the TU
bench/vcc-sema.py run -j 6 --bin-dir build/src-asan       # any harness importing vcc-diff.py
bench/vcc-diff.py batch -j 6 --cmp --kernel --bin-dir build/src-tsan   # shared header cache, 6 threads
bench/vcc-cbatch.py -j 6 --bin-dir build/src-tsan         # -c in one process, as vccd
bench/vccd-cc.py --bin-dir build/src-tsan                 # vccd itself, 150 TUs; report in $LOG
```
Both variants are `-O1 -g` with their own `obj/`; the normal build is
unchanged.  `--bin-dir DIR` (or `VCC_BIN=DIR`) picks the tools in every
harness that imports `vcc-diff.py`.  The big reservations (16 GB shared pp
region, 4 GB arenas, all MAP_NORESERVE) need no special handling; on Linux
(CI) run ASan with `ASAN_OPTIONS=detect_leaks=0` (LeakSanitizer is on by
default there; vcc exits with its arenas mapped) and the default 8 MB stack
(`ulimit -s unlimited` leaves `vstack_guard_low()` without a stack bottom).  A TSan batch takes
65-100 s (15-20x the normal build), an ASan suite 12-25 s.  Limits: ASan does
not see over-reads inside our own mmap'ed arenas (they are never poisoned),
only in malloc, globals and the stack.  Contract that ASan checks: strings
handed to `vintern*`/`lex_file_get` must be readable 16 (or 8) bytes past their
length (`vintern_cstr` pads a literal).  No suppressions are needed today;
put any in `src/tsan.supp`, one reason per entry, and run with
`TSAN_OPTIONS=suppressions=$PWD/src/tsan.supp`.

## Fuzzing (`src/test/fuzz/`)
Targets build with Homebrew LLVM's libFuzzer and ASan+UBSan into
`build/src/fuzz/` (without libFuzzer, `driver.c` gives the same command line
minus coverage feedback).
- `fuzz-lex`: lex_tokenize on raw bytes; checks token-array invariants, spelling,
  interning, NEON vs a scalar build of lex.c (`lexref.c`), buffer alignment.
- `fuzz-diag`: control bytes \x01 a source file; random records (spans,
  labels, fix-its, notes, help) through every style and output.
  `FUZZ_DIAG_PRINT=1` prints them.
- `fuzz-pp`: one in-memory TU served by `lex_file_get_hook`: main `\x01` a.h
  `\x01` b.h `\x01` sys/c.h, leading `\x02` for asm mode.
  `FUZZ_PP_PRINT=1` prints output and diagnostics.
```
make -C src fuzz                    # build the targets
src/test/fuzz/gen-corpus.py              # seed corpora from work/linux headers -> build/src/fuzz/corpus
cd build/src/fuzz && ./fuzz-pp -fork=2 -max_total_time=180 -dict=../../../src/test/fuzz/c.dict \
    -max_len=4096 -timeout=5 -artifact_prefix=art/ run-pp corpus/pp   # same for fuzz-lex
make -C src fuzz-regress            # replay src/test/fuzz/regress/{lex,pp,diag}
make -C src fuzz-diff               # vpp vs clang -E -P on regress + seeds
src/test/fuzz/pp-diff.py -j 4 --text DIR # ... byte for byte (spacing, indentation, blank lines)
src/test/fuzz/pp-diff.py -j 4 DIR...     # ... on any corpus; mismatches -> build/src/fuzz/diff/
```
Keep runs to minutes with `-fork=2`.  Every fixed finding gets a minimal
input in `regress/lex` or `regress/pp`.

## Working conventions
- All documentation, comments and messages are in English (prompts may be
  in Swedish).
- Commit after each completed step (small, self-contained commits with a
  message saying what changed and how it was verified).
- Clang is the reference: when behaviour is unclear, test it against clang
  on a small file before implementing.
- Measure before and after performance work; report numbers in the plan.
