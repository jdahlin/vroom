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
  (`ccs/vcc/vccd/caps.c`, `ccs/vcc/pp/ppdata*.inc` via `gen-ppdata.sh`).
- Do not replace make.
- The machine is shared: keep benchmarks short and use `-j 6` for
  parallel diff runs.

## Layout
- `PLAN.md`, `PLAN-fas2.md`, `PLAN-wasm.md` (WebAssembly as a target), `PLAN-x86.md`
  (x86-64 Linux as a target): plans
  and per-step status (Swedish).  Update the
  status section of a step when it changes.
- `PLAN-release.md`: what vcc 1.0 is and its release gates G0-G9 (Swedish);
  work toward the release should move a gate, and update its status there.
- `RESEARCH-preprocessor.md`: preprocessor design and prior art.
- `REVIEW-vir-lowering.md`: review of the VIR builder, format and arm64
  lowering (2026-11-12): bugs fixed and open, compile-time and runtime
  findings, spec drift, decisions needed.
- `PLAN-vir.md` (Swedish, steps V0-V7), `SPEC-vir.md` (VIR format, draft) and
  `RESEARCH-vir.md` (LLVM IR's problems): VIR, an SSA IR used only at `-O2`,
  built by compiling the shared walker a second time against a virtual
  lowering target.  Update the status of a step in `PLAN-vir.md`.
- `ccs/vcc/emit/vir/vec.inc` (#included by `build.c`): the loop vectorizer, on
  the builder's pre-IR; `VCC_VIR_VEC=0` turns it off, `r` reports each
  innermost loop to stderr, `n` keeps loop constants unhoisted (`vv_pin`).  VIR's vectors: `SPEC-vir.md` 7.5.
- `ccs/vcc/emit/vir/a64.c`: VIR's arm64 lowering.  `VCC_VIR_ROT=0` keeps VIR's block order (no loop
  rotation), `VCC_VIR_SHRINK=0` the prologue at the function's entry (no shrink-wrapping); `VCC_VL_PROBE=MASK` puts a nop after each allocator edit of the kinds in MASK, so that
  instructions retired count how often that kind runs.
- `RESEARCH-vectors.md`: vector values in VIR (how Cranelift, Wasm SIMD, B3,
  LLVM and GCC represent them; ABI and spill slots), loop vectorizers (LLVM,
  GCC, dependence rules, TSVC results here and published), the traps, and the
  proposal: lane-typed 128-bit types, a first loop vectorizer, prerequisites.
- `RESEARCH-regalloc.md`: register allocation (GCC, LLVM, SSA-based, linear
  scan and its descendants, optimal and learned methods) and the allocator
  `SPEC-vir.md` section 15 specifies for VIR, with its fallback.
- `ccs/`: our code; `ccs/Makefile` builds into `build/ccs/`.  Under `ccs/vcc/`
  generic code is in the component directories, code for one target CPU in
  `arch/<cpu>/<component>/`, code for one target OS in `platform/<os>/`
  (target, not host: host SIMD and host-OS `#ifdef`s stay with their code).
  - `lib/`: shared data structures and helpers, header-only unless noted:
    reservations, regions and arenas (`arena.h`), byte buffers (`buf.h`),
    the emitter's text buffer (`tbuf.h`), growable arrays (`vec.h`), string
    hashes (`hash.h`; XXH3/BLAKE3 in `ldhash.c`), file mapping (`mapfile.h`),
    interning (`intern.c`), binary128 (`f128.h`), thread counters
    (`vperf.h`), scoped resources through `cleanup` (`defer.h`: VAUTOFREE,
    VAUTOCLOSE, VLOCK; not across a longjmp), attributes for our own
    declarations (`attr.h`: VUSE, VNONNULL) and overflow-checked bounds for
    sizes read from input files (`ckd.h`: vckd_range, vckd_add, vckd_mul).
    Variants not yet merged are listed in TODO.md.
  - `lex/`: SIMD lexer and token cache; `lextest.c` drives it.
  - `pp/`: preprocessor (pull model, clang-exact expansion), its generated
    tables (`ppdata*.inc` via `gen-ppdata.sh`) and the include-directory cache.
  - `cc/`: parser + sema (GNU C11, clang's layout and constant rules).
  - `emit/`: code generator (straight from the AST): the shared part, state
    and types (`emitimpl.h`), the work queue and threads (`tu.c`), data
    objects, the prepass; the arm64 part is `arch/a64/emit/`.
  - `as/`: GNU as syntax (`as.c`: statements, directives, macros,
    conditionals, expressions; internal header `asimpl.h`; one module with
    `arch/<cpu>/as/parse.c`, `as_SRC`) and the standalone `vasm` driver.
  - `obj/`: the in-memory object (`obj.h`: sections, frags, symbols, fixups)
    and its layout; with `arch/<cpu>/obj/fixup.c` and `platform/linux/elf.c`
    one module (`obj_SRC`, internal header `objimpl.h`).
  - `vir/`: VIR, the SSA IR of `-O2` (SPEC-vir.md; one module `vir_SRC`, a unity
    TU like `as`, linked into no compiler tool yet): `vir.h` the public format
    (instruction, block, slot, function, the opcode table `table.c` that the
    printer, parser and verifier all read), `vir.c` the thread's reservations
    and construction (hash-consed constants, `extra` lists), `vir_cfg`,
    `vir_dom`, `vir_uses`, `vir_canon`, and the unit tables of the text form;
    `print.c`, `parse.c` (arbitrary bytes fail as `file:line:col`, never crash)
    and `verify.c` (every rule of SPEC section 12); `virimpl.h` is internal.
    Depends on nothing in `cc/`, `emit/` or `arch/`.  `virtest.c` is the tool
    (`virtest FILE`; `--no-verify`, `--bless`, `--selftest` for the builder API).
    The interpreter, `virgen` and the fuzz target (the rest of step V1) are not
    written yet.  Tests: `tests/vir/` (`bad/RULE-name.vir`, `canon/`, `dom/`).
  - `ld/`: the linker `vlink` (`ld.c`, one TU; also linked into vccd, which
    calls `ld_main` per link) and `vobjcopy`/`vnm`;
    `wasmld.c` is vlink for WebAssembly (as `wasm-ld` or with `-mwasm64`).
  - `driver/`: one compile job from a clang command line (`ppdriver.c`), the
    declaration cache (`ccsnap.c`) and `vpp`, the driver for parity tests.
  - `vccd/`: the server, the `vcc`/`vas`/`vld`/`var`/`vsh` shim (`shim.c`),
    the capability table (`caps.c`), and what runs inside vccd: kbuild's
    fixdep, the archiver (`ar.c`: thin archives, llvm-ar-identical, so the
    build uses no llvm-ar) and `rsh.c`, kbuild's recipe subset of sh, run for
    `vsh` (`build/ccs/vsh/sh`, shim.c with `-DVSH`), make's SHELL in
    `bench/vccd-kbuild.sh`; anything outside the subset goes to `$VSH_REAL`.
    Without `--backend-sock` vccd runs the whole kernel build itself: probes,
    `-E`/`-c`/`-S` (stdin too), links (vld in-process; `--ld PROG` spawns
    one), nm/objcopy behind the `vcc-nm`/`vcc-objcopy` shims, and the host
    programs (vcc + `ldmacho.c`, macOS only); nothing falls back to clang.
  - `arch/a64/`: arm64: instructions in GNU as syntax (`as/parse.c`:
    registers, `.req`, operands, literal pools), the encoder (`as/enc.c`,
    `a64.h`), system registers,
    relocation types (`reloc.h`), fixups (`obj/fixup.c`: in place or a
    relocation, as LLVM MC), the linker's relocation scan, relocation
    application and range-extension thunks (`ld/*.inc`, #included by
    `ld/ld.c` where they were: the linker is not a split module yet);
    `emit/`: instruction records and register names (`ins.h`, `a64impl.h`,
    included by `emit/emitimpl.h`), expressions, statements, calls (AAPCS64
    and Apple's), builtins, inline asm, the record optimizer (`opt.c`), the
    printer (`print.c`) and the encoder of records (`obj.c`).  It is compiled
    in the `emit` unity TU (`emit_SRC`), so VPRIV stays static.
  - `emit/walk/` and `emit/rec/`: the shared AST walker (`walk.h`: folding,
    frame slots, labels, the call sequence; it calls a CPU only through
    `LO()`) and the machine records with their per-CPU op table (`rec.h`),
    PLAN-x86.md's layers 1 and 3; `arch/x64/emit/`: x86-64's lowering
    (`x64impl.h`, `lower.c`), two-address pass and AT&T printer (`print.c`).
    x86-64 is their only user; arm64 moves to them later (TODO.md).
    `walk/prefix.h` is generated from the walker's definitions: with
    `WK_PREFIX`/`LO_PREFIX` defined the walker compiles a second time under
    other names (`bench/gen-walkprefix.py`; PLAN-vir.md).
  - `arch/x64/as/`: the x86-64 assembler, AT&T syntax with MC's encodings:
    operands, the encoder (REX and VEX) and the dispatcher in `asm.c`
    (shared header `x64as.h`), one file per instruction family (`sys.c`,
    `gpr.c`, `x87.c`, `sse.c`, `avx.c`); `vas-obj.py` checks `x86-*.s`.
  - `arch/wasm/emit/`: the WebAssembly (wasm64) code generator, AST to
    LLVM's wasm `.s` (`wimpl.h`; `wasm.c` functions, ABI, header; `wexpr.c`,
    `wcall.c`, `wstmt.c`), also in the `emit` unity TU: every name starts
    with `w`.  `emit/` picks it per target (`WASM(c)`); PLAN-wasm.md.
  - `arch/wasm/as/`: the wasm assembler (`as.c`, model in `was.h`, interface
    `wasmas.h`): LLVM's wasm `.s` to a relocatable object; `-c` for wasm64
    and `vasm --target=wasm64-...` go through it.
  - `platform/linux/`: the ELF writer (`elf.c`: relocations, string table,
    file layout as LLVM MC).
  - `platform/darwin/`: the Mach-O writer; vld's Mach-O executables
    (`ldmacho.c`: libSystem only, chained fixups, ad-hoc signature, chosen
    when the first input is Mach-O; the kernel's host programs); Apple PMU
    counters (`pmc.c`).
  - `platform/wasm/`: the wasm object writer (`wasm.c`: layout, symbol table,
    relocations as LLVM's WasmObjectWriter, byte for byte).
  - `pp/`, `cc/` and `emit/` (with `arch/<cpu>/emit/`) are directories of
    files by topic, one internal header each (`ppimpl.h`, `ccimpl.h`, `emitimpl.h`) with the types, macros
    and prototypes the files share; a shared function is `VPRIV` (static in
    the unity build).  Each such module is compiled as one TU by default
    (`obj/unity/MOD.c` #includes the files in `MOD_SRC`, SQLite style); `make
    VCC_UNITY=` compiles file by file, and every file must compile alone.
  - `phase1/`: the clang-based phase 1 tools (`vroomd`, `vroom-cc`, PCH
    prefix) and `vroom-ld`, the ld.lld wrapper the -O0 kernel build links with.
- `ccs/wasm/`: vcc (vpp) as WebAssembly for a compiler-explorer page
  (`build.sh`, emscripten memory64 -> `build/wasm/site/`); `mman.c` turns the
  big reservations into small sbrk stretches, `compat/` maps ACLE CRC to SIMDe.
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
bench/vsh-test.py                   # vsh/rsh against dash on small recipes
bench/vobjcopy-cmp.sh               # vnm/vobjcopy against llvm-nm/llvm-objcopy on the build's uses
bench/vobjcopy-any.py work/build-X  # ... and with other options on 200 random .o files
bench/vcc-driver.py                 # driver: command lines vs clang (predefines from -O), <1 s
bench/vcc-fpcheck.py -j 6           # no generated FP/SIMD code under -mgeneral-regs-only (~10 s)
bench/vcc-cfi.py -j 6               # -fasynchronous-unwind-tables: .eh_frame per function, .text unchanged (~1 min)
bench/vcc-dwarf.py -j 6             # -g line tables: dwarfdump --verify, .text unchanged, rows vs clang (~2 min)
bench/vlink-bad.py                  # corrupt objects/archives: vlink (ASan) fails cleanly (<1 s)
bench/vlink-opts.py                 # vlink options vs ld.lld: errors and loaded image (<1 s)
bench/vcc-pic.py                    # GOT and TLS relocations per -fno-pic/-fPIE/-fPIC vs clang (<1 s)
bench/vcc-volatile.py               # volatile loads/stores kept one for one, direct emitter and VIR (tests/volatile, <1 s)
bench/vir-test.py -j 6              # VIR: parser/printer round trip, verifier rules, canon, dominators (ccs/vcc/tests/vir, ~1 s)
bench/vir-exposure.py               # VIR: generated long, cyclic and multi-slot exposure checks (<1 s)
bench/vir-test.py --mutate 3000 --bin-dir build/ccs-asan   # random mutants of those units: virtest never crashes (~1 min)
bench/vcc-ctests.py -j 6 --suite vcc # our own regression tests (ccs/vcc/tests/vcc), ~1 s
bench/vcc-ctests.py -j 6 --suite inline   # gcc/llvm inlining and IPA run tests (ccs/vcc/tests/inline); VCC_VIR=bl for VIR's inliner
bench/vcc-ctests.py -j 6 --cpu cortex-a53   # vcc's binaries on a CPU without LSE (atomics as LL/SC)
VCC_LDCMP=1 bench/vcc-ctests.py -j 6 --no-cache  # also link with vlink: loaded image byte-identical to lld's (VCC_LD=vlink: run vlink's)
                                    # VCC_CFLAGS=-fasynchronous-unwind-tables: extra flags for both compilers (.eh_frame)
bench/vcc-stress.py [--vccd]        # pathological inputs fail cleanly, vccd survives (~2 s)
bench/vcc-abi.py                    # cross-compiler calls, clang/vcc x caller/callee, qemu (ccs/vcc/tests/abi, ~3 s)
bench/vcc-abi.py --host             # the same for the Apple arm64 ABI, run natively
bench/vcc-abi.py --target wasm64    # ... for wasm64 (or wasm32; --vlink), emcc + node (~20 s)
bench/vcc-self.py -j 6              # vcc.wasm built by vcc itself vs the emcc build: -S byte for byte (W6, ~40 s)
bench/vcc-darwin-s.py -j 6           # Darwin -S assembled by Apple's as, runs as the direct object (~2 min)
bench/vcc-diag.py -j 6             # tests/diag: vcc's .stderr snapshots, clang's locations/levels/flags (~2 s)
bench/vcc-diag.py --bin-dir build/ccs-asan   # ... with a sanitizer build (also vcc-mutate.py, VCC_BIN=$PWD/build/ccs-asan)
bench/vcc-diag.py --bless PATTERN   # ... rewrite the snapshots of a changed message (review the diff)
bench/gen-diagids.py [--check]      # pp/diagids.inc: each diagnostic format's id (clang's name or vcc_*)
VCC_TRACK_DIAG=1 vpp ... -fdiagnostics-show-id   # each diagnostic's id and where vcc reported it
bench/vcc-mutate.py -j 6 --formats  # ... and every mutant's JSON rebuilds its text (diag-render.py), SARIF checked, HTML page has the text
vpp ... -fdiagnostics-add-output=html:file=F   # a standalone page of the TU's diagnostics, vcc style in colour
bench/vcc-diag-catalog.py           # build/ccs/diag-catalog.html: every tests/diag case, clang/vcc/narrated/JSON side by side
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
bench/vcc-explain.py                # docs/diag pages: the wrong example gives the id, the fixed one nothing
bench/gen-explain.py [--check]      # driver/explain.inc from ccs/vcc/docs/diag/*.md (vcc --explain ID)
bench/gen-walkprefix.py [--check]   # emit/walk/prefix.h: the walker's function names, WK_PREFIX compiles it under another name (VIR)
build/ccs/viralloc --sweep F.vir     # VIR register allocation (vir/alloc.c) on a synthetic target: dump + counters, checker at every register count
bench/vir-alloc.py [--bless]        # ccs/vcc/tests/vir-alloc: expected dumps and sweeps (<1 s); `random -n N`: generated functions (~1 s/unit)
bench/vir-alloc.py mutate|time      # the checker catches corrupted allocations; ns per VIR instruction (budget 200 for now, target 60: SPEC 22.1)
bench/vcc-bflayout.py               # generated bit-field records, layouts checked by clang, Linux + Apple (~1 s)
bench/vcc-ldconst.py                # generated long double literals and folds, bits vs clang (~2 s)
bench/vcc-f80.sh [N]                # x87 long double folding (lib/f128.h) vs this x86-64 host's FPU (~3 s)
bench/vccd-test.py                  # vccd as a service: socket, bad requests, cache invalidation (~11 s)
vcc --vccd-diagnostics[=json]       # vccd's diagnostics store: summary by flag/file, or every TU's records (JSON lines)
VCC_DIAG_DEDUP=1 make ...           # under vccd: a header warning another TU of the build printed is left out
bench/vccd-kbuild.sh work/build-X -j 6   # cold kernel build, vccd only (no clang/vroomd/lld; ~10 s)
python3 bench/boot-test.py --kernel work/build-X/arch/arm64/boot/Image --timeout 60   # boots Debian
bench/vbench.py gate                # performance gate: instructions vs the merge-base, budget 1 % (~1 min)
bench/vcc-wasm.py -j 6              # the C suites for wasm64: vcc -S, clang -c, emcc, node vs clang's build (~2 min)
bench/vcc-x86.py -j 6               # the C suites for x86-64 on an x86-64 host: vcc -S, clang -c, glibc, native (~10 s; first run ~10 min for the references)
bench/vcc-wasm.py -j 6 --objcmp     # ... and vpp -c's object byte-identical to clang's for the same .s
bench/vcc-wasm.py -j 6 --obj --vlink  # ... vcc's side through vpp -c and vlink as emcc's wasm-ld (~9 min)
bench/vcc-same.py --base DIR -j 6    # test-suite outputs byte-identical with DIR/vpp (vbench.py mkbase REV), ~80 s
python3 bench/virgen-test.py --bin-dir build/linux/ccs  # generated VIR integer/control-flow units verify and round-trip
bench/vas-enc.py $PWD/ccs/vcc/tests/vas   # vas encodings vs llvm-mc
bench/vas-obj.py                    # vasm objects vs clang -c byte for byte (ccs/vcc/tests/vas-obj, <1 s)
bench/vas-corpus.py -j 6 [-O2] [PAT] # clang -S of the test C files: clang -c vs vasm (~6 min for -O0 -O2)
bench/pg/build.sh clang && bench/pg/build.sh vcc   # PostgreSQL (macOS, meson+ninja): build + meson test (~2 min each)
bench/pg/build.sh vccd             # ... CC = vcc shim + private vccd (VCC_DELEGATE=bench/pg/cc for the rest)
                                   # build: "warnings: N, not from clang: M" (M must be 0; logs in work/build-pg-*.build.log)
bench/pg/speed.py                  # PostgreSQL compile speed: clang, clang -O0, vpp, vpp --batch
bench/sqlite/run.py all            # SQLite (macOS): vcc/clang/gcc builds, TCL testrunner (no vcc-only fails), speedtest1 (~4 min)
bench/zlib/run.py all              # zlib 1.4 (macOS): clang/vcc/vir/gcc builds, self-tests, same bytes as clang's, zbench CPU time (~30 s)
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
`meson test` set.  A bug found there gets a test in `ccs/vcc/tests/vcc/`.
`TODO.md` lists what is left before vcc is a production compiler, each item
with how it is validated.  The vcc suite, stress and vccd tests mark known bugs
`xfail` with their TODO.md item; a fix that makes one pass turns it into an
XPASS error, so the same commit removes the marker.
Parity is a token-stream comparison (kind and spelling) of `clang -E -P`
against `vpp`, both lexed by vcc's lexer.  A change to `pp/` or `lex.c` is
only done when the parity run is fully green; a change to `cc/` when both
vcc-sema.py runs are green (878/879: modpost.c lacks a generated header).

## CI and the premerge gate
`.github/workflows/premerge.yml` runs one reusable workflow per target in
parallel (`arm64.yml`: jobs arm64, asan, tsan; `wasm.yml`; x86-64 gets its
own), each on GitHub's ubuntu-24.04-arm (tsan and wasm on x86-64's
ubuntu-24.04 for now) in a Debian 13 container
(`ci/setup-debian.sh`: clang 23 from apt.llvm.org, qemu, emsdk for wasm).
The test set is `ci/premerge.sh JOB`: the vendored C suites under qemu and
the fast harnesses, no kernel, PostgreSQL or anything needing `work/`.
Locally, as CI: `env/linux/run.sh ci/premerge.sh arm64`.  main is not
protected (private repo, free plan), so the gate is the workflow: push to
main through `ci/premerge-push [--watch]` (pushes to `premerge`; job `gate`
fast-forwards main when everything is green).  A new harness belongs in
`ci/premerge.sh` when it is fast and needs only the checkout.

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
region, 4 GB arenas, all MAP_NORESERVE) need no special handling; on Linux
(CI) run ASan with `ASAN_OPTIONS=detect_leaks=0` (LeakSanitizer is on by
default there; vcc exits with its arenas mapped) and the default 8 MB stack
(`ulimit -s unlimited` leaves `vstack_low()` without a stack bottom).  A TSan batch takes
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
- `fuzz-diag`: control bytes \x01 a source file; random records (spans,
  labels, fix-its, notes, help) through every style and output.
  `FUZZ_DIAG_PRINT=1` prints them.
- `fuzz-pp`: one in-memory TU served by `vlex_get_hook`: main `\x01` a.h
  `\x01` b.h `\x01` sys/c.h, leading `\x02` for asm mode.
  `FUZZ_PP_PRINT=1` prints output and diagnostics.
```
make -C ccs fuzz                    # build the targets
ccs/fuzz/gen-corpus.py              # seed corpora from work/linux headers -> build/ccs/fuzz/corpus
cd build/ccs/fuzz && ./fuzz-pp -fork=2 -max_total_time=180 -dict=../../../ccs/fuzz/c.dict \
    -max_len=4096 -timeout=5 -artifact_prefix=art/ run-pp corpus/pp   # same for fuzz-lex
make -C ccs fuzz-regress            # replay ccs/fuzz/regress/{lex,pp,diag}
make -C ccs fuzz-diff               # vpp vs clang -E -P on regress + seeds
ccs/fuzz/pp-diff.py -j 4 --text DIR # ... byte for byte (spacing, indentation, blank lines)
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
