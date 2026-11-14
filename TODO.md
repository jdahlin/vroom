# vcc: TODO before a production compiler

From the production-readiness review of 2026-11-08 (six component reviews,
probes against clang 23).  Items are ordered by risk: tier 1 is silent wrong
code, tier 2 makes vccd safe to keep running, tier 3 is missing features,
tier 4 is quality.  Every item names where it lives and how it is validated;
an item is done when its test exists, fails before the fix and passes after,
and the existing gates (AGENTS.md "Build and test") stay green.

Notation: **[P]** confirmed by a probe during the review, **[R]** from reading.

File references: `emit.c:N`, `cc.c:N` and `pp.c:N` predate the split into
`codegen/` (+ `arch/a64/codegen/`), `frontend/c/` and `frontend/pp/` (2026-11-08), and line numbers
in `as/as.c`, `obj/obj.c` and `ld/ld.c` predate the restructure of
2026-11-09 (AGENTS.md "Layout"), which moved parts of them to `arch/a64/`
and `platform/linux/` and shortened the files whose buffers moved to `lib/`
(`utils/ar.c`, `utils/shell.c`, `utils/fixdep.c`, `frontend/c/layout.c`, `frontend/diag/diag.c`);
search for the function named in the item.
The test kinds referenced below are defined in the "Test infrastructure" section
first; most items only add a file to one of them.

## Test infrastructure (build these first)

Existing gates that every change must keep green: `vcc-diff.py cmp`
(pp parity), `vcc-sema.py run` + `layout` (878/879), `vcc-ctests.py`
(1708/1708), `vcc-diag.py` (63/63), `vcc-sema.py warn` (879/879),
`make -C src self-check`, `make -C src fuzz-regress`, kernel boot.

- [x] **T0 performance gate.** (done: `bench/vbench.py gate`.)  Every item below must leave the hot path
  alone: `bench/vbench.py` (kernel front end + emit, fixed corpus) before
  and after each change, budget +1 % per item, numbers in the commit
  message.  Features that cost time when used (`-g`, warnings that need
  dataflow, an optimizer) are measured with the flag on and off separately.
- [x] **T1 `vcc` suite: self-checking regressions under qemu.** (done: `bench/vcc-ctests.py --suite vcc` with xfail markers, 9 tests added, all xfail today.)
  `src/test/vcc/*.c`, gcc-torture style (exit 0 or `abort()`), the
  suite in `bench/vcc-ctests.py` (`tests()`: `--suite vcc`).  Clang
  is still the oracle, so a test also documents clang's behaviour.  Supports
  `/* vcc-flags: ... */` and `/* vcc-threads */` (link the runtime's tiny
  thread shim, or run on the Darwin host, see T3).  One file per fixed bug.
- [ ] **T2 `abi` suite: cross-compiler calls.** (partial: `bench/vcc-abi.py`, Linux/qemu, `--host` Darwin and `--target wasm64|wasm32` halves, xfail markers; no generator yet.)  `src/test/abi/NAME/{caller.c,callee.c}`;
  the runner builds the 4 combinations (clang/vcc × caller/callee) and runs
  each, on Linux (qemu, `test/rt`) and on the Darwin host (native, system
  libc).  Covers argument passing, returns, varargs, layout of shared structs.
  A generator (`bench/abi-gen.py`, random signatures with a fixed seed, like
  gcc's `struct-layout-1`) gives breadth; hand-written cases pin known bugs.
- [ ] **T3 `host` suite: native Darwin runs.**  Same `vcc` suite files built with
  `vpp -c --target=arm64-apple-darwin`, linked with ld64 against libSystem,
  run natively.  Needed for threads, TLV, real libm, and Darwin ABI items.
  `make -C src host-tests`.
- [ ] **T4 `reject` suite: must-diagnose tests.**  `test/diag/` already
  diffs stderr exactly with clang; add `test/reject/*.c` where only "vcc
  exits non-zero with an error mentioning X" is required (for cases where an
  exact clang message match is not yet the goal).  `// expect-error: REGEX`
  lines; runner `bench/vcc-reject.py`.  Promote to `diag/` once messages match.
- [x] **T5 `stress` suite: crash containment.** (done: `bench/vcc-stress.py [--vccd]`, 19 cases, 15 xfail.)  `bench/vcc-stress.py`
  generates pathological inputs (200k nested parens / unary minus / `?:` /
  `#if` parens / macro calls, 10k struct members, 5000 `-I`, a 2 MB function,
  `.rept 1e8`, 17M-token expansion, 2^31-byte file via sparse file) and
  requires, for each: no signal, exit status 1, a diagnostic on stderr,
  within a time bound.  A `--vccd` mode sends them all to one private vccd
  (as `vccd-cc.py` does) and then a normal TU, which must still compile.
  Run also with `--bin-dir build/src-asan`.
- [ ] **T6 `obj` checks: object-level assertions.**  `bench/vcc-objcheck.py`
  with `test/obj/*.c` carrying `// CHECK-RELOC: R_AARCH64_ADR_GOT_PAGE sym`,
  `// CHECK-SECTION: .eh_frame`, `// CHECK-NOT-SYM: x`, checked with
  `llvm-readelf -r -S -s` / `llvm-objdump -d`.  For PIC, TLS, CFI, DWARF,
  section groups, branch ranges — things that are about the object, not
  runtime output.
- [ ] **T7 `as` corpus: compiler-generated assembly.** (started: `bench/vas-corpus.py`: clang -S -O0/-O2 of every test C file, clang -c vs vasm byte for byte: 3035/3035 identical on 2026-11-09, `-g`, `-O2 -fPIC`, `-O3`, `-O3 -march=armv8.6-a+...` and `-O3 -march=armv9-a+sve2`, `-O3 -mcpu=neoverse-v1` (`.prefalign`) too.)  `test/as/*.c` →
  `clang --target=aarch64-linux-gnu -O2 -S` (and `-g`, `-fPIC`, `-march=armv8.5-a+sve2`)
  → assemble with `vasm` and with clang, compare objects with
  `bench/vas-diff.py` (sections, symbols, relocations, bytes).  Plus
  per-instruction encoding tables generated from llvm-mc
  (`bench/vas-enc.py` extended: every mnemonic × operand form in a list,
  diff `llvm-objdump -d` text).
- [ ] **T8 `ld` suite: links against lld.**  `test/ld/NAME/` with sources,
  a command line and an optional script; `bench/vld-cmp.py` links with
  `vlink` and `ld.lld`, compares `llvm-readelf -a` (normalised), and where
  runnable, runs both under qemu-user or qemu-system and compares output.
- [ ] **T9 more fuzz targets** (`src/test/fuzz/`): `fuzz-cc` (front end on pp
  output, `-fsyntax-only`), `fuzz-emit` (front end + emit to a buffer; with
  csmith/yarpgen programs as a differential oracle under qemu), `fuzz-as`
  (assembler text), `fuzz-elf` (ld `parse_obj`/`add_archive`, objcopy
  `elf_open`, nm on raw bytes), `fuzz-ar`.  Each with `regress/<target>/`
  replayed by `make -C src fuzz-regress`.
- [ ] **T10 random-program differential testing.**  `bench/vcc-csmith.py`:
  csmith / yarpgen programs (checksum output) built by clang -O0 and vcc,
  run under qemu, compare.  Minutes per run, reduce failures with creduce
  into T1 tests.  Run natively on the Darwin host too (Mach-O backend, no
  qemu: thousands of programs an hour at `-j 6`), and steer csmith towards
  what the kernel uses (bit-fields, packed structs, `volatile`, structs as
  arguments and returns).  Acceptance: 24 h per generator without a
  mismatch, on both targets.
- [ ] **T11 driver tests.** (started: `bench/vcc-driver.py`, -O predefines.)  `bench/vcc-driver.py` with a table of
  command lines → expected (exit status, output files, stderr regex),
  including flags that must be rejected.  Uses `vpp` directly and through a
  private vccd.
- [x] **T12 vccd service tests.** (partly done: `bench/vccd-test.py`, 12 tests, 9 xfail, peer check skipped without sudo; not yet: cancel on disconnect, SIGTERM, version mismatch, memory after N builds, fd leaks into children.)  `bench/vccd-test.py`: starts a private
  vccd and checks socket permissions, peer rejection (a second uid via
  `sudo -u nobody` when available, otherwise a forged-uid unit test of the
  check), bad requests (wrong fd count, truncated messages, oversized env),
  idle-connection timeout, cancel on client disconnect, stale-cache
  invalidation after `touch`/edit/create, version mismatch, memory after
  N builds.
- [ ] **Linux x86 corpus blockers found through VIR (2026-11-12)** [P].
  PostgreSQL 18.6: 13 of 1504 compile jobs reject clang 23's intrinsic
  headers: missing `__builtin_ia32_pmovmskb128`,
  `__builtin_elementwise_sub_sat` and `__builtin_elementwise_min`.
  CPython 3.13.2: four of 334 compile jobs reject
  `__builtin_flt_rounds` (Objects/floatobject.c, Python/dtoa.c) or MMX/SSE
  packing/saturation builtins (Modules/_blake2/blake2{b,s}_impl.c:
  `__builtin_ia32_emms`, `__builtin_ia32_packsswb128`,
  `__builtin_ia32_packssdw128`, `__builtin_ia32_packuswb128`, elementwise
  add/sub saturation). With those TUs explicitly built by clang, CPython
  also lacks `_mi_page_malloc`, `_mi_heap_malloc_zero_ex`,
  `_mi_bitmap_try_find_claim_field` and `_mm_pause` at link time.
  Exact obmalloc.c commands reproduce all four undefined symbols with VIR
  both off and on; clang defines the three mimalloc helpers and emits no
  `_mm_pause` call. Their inline/linkage path needs investigation, not a
  VIR-only workaround. *Validate:* minimal builtins/inline regressions
  against clang; all original compile commands must pass without compiler
  fallback, then full native project tests. Source versions, raw diagnostics
  and command records: work/vir-x86-corpus/{manifest.json,logs}; corpus runner
  bench/vcc-vir-corpus.py. Gate/status: PLAN-vir.md V2/V3.
- [ ] **T13 real programs with their own test suites.**  Like PostgreSQL
  (42ecfb3, `meson test` as the oracle): build each with clang and with vcc
  and compare test outcomes, not just "green".  Candidates with strong
  self-checking tests: sqlite (`testfixture` + tcl tests, `speedtest1`), lua
  (official test suite), zlib/zstd/xz (round trips and checksums over a
  corpus), musl + libc-test (integer, string and FP edge cases), CPython
  (`make test`).  Darwin host first, Linux user space once PIC/PIE exists.
  SQLite 3.50.4 on Darwin (2026-11-11): vcc builds testfixture + shell via
  `bench/pg/cc`; `testrunner.tcl --jobs 6` gives the same result as clang
  -O2 (329068 tests, only interrupt2.test fails, timing, both compilers)
  after the Apple variadic-slot fix (test/abi/va_int_slot).  speedtest1
  --size 100 --memdb: clang -O2 2.49 s, gcc-16 -O2 2.44 s, vcc -O2 3.92 s;
  sqlite3.c compiles in 0.13 s (clang -O2 7.9 s, gcc 22 s).  `bench/sqlite/run.py all`.
  zlib 1.4 on Darwin (2026-11-12, `bench/zlib/run.py all`): vcc and VIR
  (`VCC_VIR=bl`) pass example, infcover and the minigzip round trips, and
  deflate at levels 1/6/9 gives clang's bytes.  All 131 functions VIR sees
  are lowered; only `crc32_z` (inline asm) falls back.  zbench on zlib's own
  source tar, CPU time against clang -O2: vcc 1.37x, VIR 1.10x, gcc-16 0.99x
  (VIR: deflate-1 1.32x, deflate-6 1.12x, deflate-9 1.09x, inflate 1.00-1.04x,
  crc32 1.18x, adler32 0.98x).  Where VIR loses (sample profile):
  `fill_window` 19x clang's share (clang vectorizes `slide_hash`),
  `deflate_fast` 1.6x, `compress_block` 1.25x, `pqdownheap` not inlined into
  `build_tree`; `.text` 79896 bytes against clang's 71598 and vcc's 72580.
  After the allocator's parameter vote and load reuse across blocks
  (PLAN-vir.md, 2026-11-12): VIR 1.03x in total, deflate-9 1.00x, deflate-6
  1.09x, deflate-1 1.36x; `.text` 78304.  After own field addresses, narrow
  stores, sub+cmp and csel with zero: total 1.02x, deflate-1 1.19x, deflate-6
  1.02x; `.text` 74436.  Left on level 1: vectorizing `slide_hash`, and
  type-based aliasing for `deflate_fast` (a flag, not decided).
- [ ] **T14 the kernel runs, not just boots.**  KUnit built in
  (`CONFIG_KUNIT=y` plus the common suites, run at boot, TAP on the
  console), compared with the clang build's TAP; then kselftests and an LTP
  subset in the Debian guest (syscalls, mm, sched, loopback net); the
  modules kernel booted with `modprobe` of a sample of modules; a debug
  config (`PROVE_LOCKING`, `DEBUG_ATOMIC_SLEEP`) that catches bugs like the
  `scoped_guard` double unlock from PLAN-fas2 2.6.
- [ ] **T15 code quality as numbers.**  Ratios against clang `-O0` (floor)
  and `-O2` (target), A/B on a quiet machine.  Static, every commit: text
  size and instruction count per kernel TU (`vcc-emit-stats.py`), totals
  and the 20 largest differences.  Dynamic: run time natively on Darwin
  (sqlite `speedtest1`, lua benchmarks, zstd, CoreMark, Embench,
  llvm-test-suite SingleSource/Benchmarks); under qemu, executed instruction
  counts from the TCG `insn` plugin instead of time (deterministic,
  independent of machine load).  Feeds the Tier 4 optimizer item.
- [ ] **T16 coverage of vcc itself.**  Build vcc with
  `-fprofile-instr-generate -fcoverage-mapping`, run all suites, and list
  `frontend/c/` and `codegen/` branches no suite reaches; each gets a targeted T1 test.
  This, not the number of packages built, says when testing is enough.
- [ ] **T17 Debian, sampled, last.**  A full archive rebuild mostly tests
  the front end ("it built" says little about wrong code; most packages
  have weak tests; Debian's clang rebuilds found mainly front-end
  rejections), and needs Linux user space (PIC/PIE, shared libraries, glibc
  headers, Tier 3).  So in steps: (1) front end only, `vpp -fsyntax-only` +
  `vcc-sema.py layout`-style checks over preprocessed sources (from a clang
  build) of the ~500 C packages highest in popcon; (2) build +
  `dh_auto_test` for ~100-200 packages with good tests (coreutils, grep,
  sed, gawk, bash, openssl, curl, libxml2, glib), the clang build as
  reference; (3) the whole archive only if (1)-(2) still find bugs at a
  rate that justifies it.
- [x] **rt `%Lg` with clang's caller**: `%g` read its exponent with
  `strtol` from an unterminated buffer, so stack bytes after it decided
  between `19` and `1.9e+01`.  Done: `test/vcc/printf_Lg.c`,
  `deep_mixed.c` prints `%Lg` again.
- [ ] **T18 runtime gaps hide torture tests.**  34 gcc/tcc tests are
  `ref-fail` (`vcc-ctests.py --show ref-fail`): ~15 only because
  `test/rt` lacks `pow`, `tanh`, `__divsc3`, `__fixsfti`, `tmpnam`,
  `atomic_signal_fence`, or clang leaves `__builtin_exit`/`__builtin_puts`/
  `__builtin_mul_overflow_p` as calls.  Add them to the runtime (or a
  per-test shim) so the tests run.  *Validate:* `ref-fail` count drops,
  the newly run tests pass or become xfail with a TODO item.
  (Done: rt has a small libm (exp/log/pow/trig/hyperbolic/frexp),
  `__divsc3`, `__fixsfti`, `tmpnam`, `atomic_signal_fence` and the
  `__builtin_exit`/`puts`/`isinff` calls; rt's `sqrt` recursed into itself
  under `-fno-builtin`.  ref-fail 34 -> 25, all 9 pass.  Left: the clang
  builds that abort or time out (16), `__builtin_mul_overflow_p`,
  `__builtin_apply_args`, `__builtin_va_arg_pack`.)
- [ ] **T19 ASan inside our arenas.**  Poison the mmap'ed arenas behind
  `used` (`ASAN_POISON_MEMORY_REGION` under `__has_feature`), so
  over-reads there are reported.  *Validate:* an injected over-read in a
  test build is reported; ASan runs of the gates stay clean.
- [ ] **T20 clang's -verify tests.**  (started: `bench/vcc-verify.py`.)
  clang/test/{C,Sema,Preprocessor,Lexer,Parser} (`bench/fetch-testsuites.sh
  llvm`): each `%clang_cc1 -verify` RUN line whose rewritten command clang
  23 passes is run by vcc and its diagnostics matched against the
  `expected-*` directives.  2026-11-10: 412/1841 pass (C/drs 29/70, C/C2y
  32/95, Sema 236/1276), 1 crash (Sema/attr-tls_model.c).  Largest gaps:
  SVE/SME types and builtins (`__SVInt8_t`, streaming checks), `__bf16`,
  `__mfp8`, distinct `nullptr_t` identity for constexpr bit casts, `_Countof`, `__scoped_atomic_*`,
  `_Static_assert` in a struct, `//` comments under -std=c89.  *Validate:*
  no crash; pass count only goes up (results.json lists regressions).

Order once Tier 1 is clear: T10 (finds most wrong code per hour), T2's
generator, T14 KUnit, T15 static sizes, T13, T16, T17 after PIC/PIE.

## Tier 1: silent wrong code

- [ ] **arm64: unary minus on `__int128` vector lanes keeps 32 bits.**
  `-a` for `a` a `vector_size(32)` of `__int128` gives `4294967293` for a
  lane of 3 (`gen_vlanes` negates through `arith` at the element's width as
  if 32 bits).  x86-64's walker does it right.
  *Validate:* `src/test/vcc/vec_wide_lanes.c` (xfail on arm64).

- [ ] **arm64: the hardening options are accepted and ignored.**
  `-ftrivial-auto-var-init=zero`, `-fstack-protector*`, `-fpatchable-function-entry`
  (and on x86 only `-fcf-protection`, `-mfunction-return=`, `-mretpoline-external-thunk`)
  are implemented for x86-64 (cc_options.xcf ...); arm64 accepts them silently,
  so a kernel built with them lacks the protection.
  *Validate:* `src/test/vcc/autoinit_zero.c` (xfail on arm64).

- [ ] **arm64: asm flag outputs (`"=@cc<cond>"`) are not implemented.**
  The output gets an unset register (`cmp w9, #0` then `str w10`): any
  `"=@cceq"` reads garbage.  x86-64 has them (`set<cond>` + `movzbl`; the
  kernel's CC_SET in test_bit/boot_cpu_has).
  *Validate:* `src/test/vcc/asm_flag_out.c` with an arm64 branch.

- [x] **Branch range: no relaxation for b.cond/cbz/tbz/adr** [P].  Done for
  functions without inline asm (`r_relax` in emit.c: b.!cond .+8; b label,
  iterated to a fixed point, only for functions over 1 MB); with inline asm,
  and for `adr` to a label beyond 1 MB, an error instead of wrapped code
  (relaxation there is still to do).  The emitter has no tbz.  Left: the
  assembler's integer targets (`b.eq 0x100000000`), see the assembler section.
  `include/arch/a64/a64.h:129-140` mask the offset; `enc()` in `emit.c:1484`.  Out-of-range
  conditional branches must become an inverted branch over a `b`; `adr`
  beyond ±1 MB must become `adrp+add`; `tbz` (±32 KB) likewise.  Also assert
  in the encoders (debug builds) that the offset fits.  Assembler too:
  `b.eq 0x100000000` is accepted silently [P].
  *Validate:* T1 `branch_far.c` (generated: a function > 1.5 MB with
  `if`/loops/`switch` around the bulk, plus a > 32 KB `tbz` case); T6 checks
  no out-of-range immediate via `llvm-objdump` targets; T4 for the
  assembler's out-of-range error.
- [x] **Over-aligned locals clamped to 16** [P].  Done: the prologue
  realigns sp (42ecfb3, from PostgreSQL); in a function with a VLA or
  alloca x28 holds that realigned sp and addresses the frame slots
  (finish_function); VLAs of over-aligned types round their address up
  (codegen/stmt.c).  `slot()` `emit.c:2224`.
  Realign: when a function has a local with alignment > 16, align sp
  (`and sp, x?, #-A`) after the frame setup and address such locals off a
  base register (x19 or sp), keeping x29 for the frame.
  *Validate:* T1 `align_local.c` (`_Alignas(32/64/128/4096)` locals, arrays
  and VLAs of over-aligned types, inside recursion and alongside alloca;
  check `(uintptr_t)&x % A == 0`).
- [x] **Atomic complex increment/decrement** [P]. Non-atomic complex
  `++`/`--` update only the real component and preserve the imaginary bits.
  Atomic complex values up to eight bytes use a whole-object compare-exchange
  loop (`test/vcc/atomic_complex_incdec.c`, `test/atomic/complex_concurrent.c`).
  Linux wider complex values use the generic libatomic whole-object ABI and
  CAS retries. Validated concurrent double/long-double increments, imaginary
  signed-zero retention, 64-byte aggregate snapshots and 128-bit integer RMW
  on both Linux targets (`test/atomic/wide.c`, link with `-latomic -pthread`).
  Wider atomic operations on other targets remain unsupported.

- [x] **`_Atomic` dropped** [P].  Done: `_Atomic(T)` and the qualifier
  (also `T *_Atomic`) make a type of its own (`AST_TYPE_FLAG_ATOMIC`, `ty_atomic()` in
  frontend/c/type.c) with clang's layout; lvalue conversion (`decay_slow`) yields
  T; emit loads with `ldar`, stores with `stlr`, and every compound
  assignment and `++`/`--` is a `casal` loop around the operator's usual
  code (`atomic_rmw`, codegen/expr.c).  Tests: `atomic_layout.c`,
  `atomic_ops.c`. Linux whole-object records and wider integer, FP and complex
  accesses use the libatomic pointer ABI when a native 1/2/4/8-byte access is
  unavailable; native 16-byte instructions remain an optimization. Atomic
  vector types remain unsupported by the frontend.
  Lock-free queries fold guaranteed native 1/2/4/8-byte aligned accesses;
  `__atomic_always_lock_free` is an integer constant expression and leaves its
  pointer unevaluated. Unknown Linux sizes/alignment query libatomic instead
  of claiming every access is lock-free. Library-backed 16-byte operations
  are conservatively not always lock-free. Tests: `atomic_lock_free.c`,
  hosted `test/atomic/lock_free_runtime.c` (both Linux targets).
  Differs from clang on
  purpose: clang 21/23 zero-extend a signed old value when the compound
  operation leaves int arithmetic (`x /= -2` on `_Atomic signed char` -2
  gives -127); vcc gives 1 as C says.  Clang rejects `+=` on an atomic
  pointer, vcc accepts it.  `ld<op>al` for `+ - & | ^` would save the loop
  (performance only).
  Was: keep the
  qualifier on the type; lower loads/stores to `ldar`/`stlr`, compound
  assignment and `++`/`--` to LSE (or CAS loops for types without a
  direct op, and for floating point), and lay out `_Atomic(T)` as clang does
  (size rounded to a power of 2 up to 16, alignment = size).
  *Validate:* T6 `atomic-ops.c` (no plain `ldr/str` for atomic accesses;
  checked by hand with llvm-objdump until T6 exists);
  T1/T3 `atomic-threads.c` (4 threads × 1M `x++` on `_Atomic int`,
  `_Atomic long double`, `_Atomic struct{char[3]}` → exact count);
  `_Static_assert(sizeof/alignof)` cases in `vcc-sema.py layout` style.
- [x] **Generic rotations truncate operands/counts above 128 bits** [P].
  Keep the unsigned wide operand type and the full unsigned count type before
  normalization. `stdc_rotate_widths.c` covers unsigned scalar/BitInt widths,
  return types, once-only evaluation and large counts. Unchanged GCC
  `builtin-stdc-rotate-1.c` passes; its >128-bit section is x86-only because ARM
  advertises `__BITINT_MAXWIDTH__ == 128`. Clang cannot lower runtime-count
  funnel shifts above 128 bits: retained separately as an oracle limitation,
  while the durable wide checks use constant counts and runtime values.
  *Validate:* unity/file builds, rotation tests O0/O2/O3 x86 then ARM/QEMU,
  original GCC case, sema run/batch; `work/results/stdc-rotate/validation.json`.
- [x] **Inlined alloca accumulates across repeated calls** [P]. Linux x86/ARM
  restore the inline function's entry SP at its common return label, after
  argument evaluation. Callee alloca no longer rewrites caller VLA restore
  points. Direct allocations in the containing function retain function
  lifetime. Regression `inline_alloca_lifetime.c` covers nested inline calls,
  early returns and live caller VLA/alloca objects; unchanged GCC
  `gcc.dg/pr113596.c` retains its `-O2` contract.
  *Validate:* unity/file builds, own x86 then ARM/QEMU suites, O0 regression,
  and kernel sema run/batch; evidence `work/results/pr113596/validation.json`.
- [x] **VLA pointer `++`/`--`/`+=`/`-=`/difference ignore runtime size** [P].
  Done (codegen/expr.c: assign_op, incdec_op, pointer difference).  Found
  with it: `int a[6][n]` (constant outer bound) was laid out as a 0-byte
  fixed array that overwrote other locals; an array of VLAs is now a VLA
  (frontend/c/declarator.c).  VLA sizes are evaluated once where the declaration
  is reached (parameters: on entry) and kept in a slot (codegen/stmt.c
  vla_fix), so a later `n = ...` no longer changes `sizeof` or the step.
  Was: `ptr_add_vla` `emit.c:6051` only covers binary `+`/`-` and subscripting.
  *Validate:* T1 `vla_ptr.c` (`int (*q)[n]`: `q++`, `q--`, `q += 2`,
  `q -= 1`, `q3 - q`, `&q[1] - q`, 2-D VLA of VLA).
- [x] **Adjacent string literals decode before the final prefix is known** [P].
  Collect the complete sequence before decoding so late `u`/`U`/`L` prefixes
  determine the encoding and escape range; reject incompatible prefixes.
  *Validate:* `string_concat_late_prefix.c`, diagnostic `string_concat_prefix.c`;
  unchanged Kefir `string_literals1` and `wide_string_literals2` on x86-64.
  Their signed-`wchar_t` assumptions are rejected by Clang on ARM; the
  portable regression covers ARM/QEMU instead.
- [x] **Foldable VLA bounds lose evaluated side effects** [P]. Array
  bounds such as `(callback(), 1)` retain their runtime expression even
  when folding produces an integer. Unevaluated calls in an integer constant
  expression still permit a fixed array.
  *Validate:* `vla_bound_effects.c`; unchanged Kefir `vla1`, all four
  Clang/vcc combinations on Linux x86-64 and ARM/QEMU.
- [x] **VLA with initializer accepted; garbage at run time** [P].  Done:
  rejected as clang does, same location and range (frontend/c/decl.c); `= {}`
  (accepted by clang in every mode) zeroes the array at run time
  (codegen/stmt.c gen_vla_decl).  Tests: `test/diag/vla_init.c`,
  `test/vcc/vla_zero.c`.
  Was: reject
  as clang does ("variable-sized object may not be initialized"), except
  `= {}` in C23 which zeroes.
  *Validate:* T4 (`int a[n] = {1};` errors); `test/diag/vla_init.c` once
  the message matches; T1 for the C23 `= {}` zeroing when C23 lands.
- [x] **Vector types passed in GPRs** [P].  Done: `hfa()` (codegen/abi.c)
  takes short vectors (8/16 bytes) alone and as HVA members (1-4 of one
  size, never mixed with floats), so arguments, returns and va_arg use v
  registers; vectors under 8 bytes are returned in v0 as clang/LLVM does
  (lanes widened to 8 bytes; `vec_small`).  Tests: `test/abi/vec_args`,
  `test/abi/vec_small_ret` (bench/vcc-abi.py, Linux).  Left: the Darwin
  host half (T2/T3), variadic vectors on Darwin.
  Was: `is_recabi` `emit.c:98`.
  Short vectors (8/16 bytes) go in v registers per AAPCS64 (and HVA
  classification for structs of them, see tier 3).
  *Validate:* T2 `vec-args` (int32x4, uint8x8, `vector_size(16)` float/int,
  as arguments, returns, varargs, mixed with HFA) both targets.
- [x] **Darwin: narrow integer returns not extended** [P].  (done:
  `test/abi/narrow_int`, `bench/vcc-abi.py --host`; arguments too.)  Apple's ABI
  requires the callee to sign/zero-extend `char`/`short`/`bool` returns to
  32 bits (and the caller for arguments).  Extend at return in emit.c when
  the target is Darwin.
  *Validate:* T2 `narrow-ret` on Darwin (`retc(0x1ff)` → -1, `bool`,
  `unsigned short`, through function pointers and tail position).
- [x] **Darwin: unnamed bitfields add alignment** [P].  (done:
  `bench/vcc-bflayout.py`, both targets, and `test/abi/bitfield_layout`;
  it also found two Linux bugs, fixed with it: `aligned()` on a bit-field
  did not move its offset, and packed bit-fields were byte- not bit-aligned.)  On Darwin `int :0`
  and `int :3` do not affect the struct's alignment.  `struct{char a; int :0;
  char b;}` is 8 in vcc, 5 in Apple clang.
  *Validate:* layout `_Static_assert` table for `--target=arm64-apple-darwin`
  (extend `vcc-sema.py layout` with a Darwin target over a generated
  bitfield corpus, as for Linux); T2 `bitfield-layout` on Darwin.
- [x] **`long double` constants folded with strtod on Linux** [P].  (done:
  `f128.h`, `bench/vcc-ldconst.py`; also float literals rounded once, and
  out-of-range float→int folds saturate as clang.)
  `number_s` `cc.c:1296`.  Parse to binary128 (a correctly rounded
  decimal→binary128 converter; the tf soft-float in `test/rt` can be the
  check), fold in binary128.
  *Validate:* T1 `ldouble_const.c` (`0.1L != 0.1`, `1e4000L` finite,
  `LDBL_MAX`, `LDBL_MIN`, denormals, hex float `0x1.0000000000000000000000000001p0L`,
  bit patterns compared via `memcpy` against clang's).
- [x] **`#pragma weak` ignored** [P].  `cc.c:892` handles only `pack`.
  Implement `#pragma weak sym` and `#pragma weak alias = target`.
  *Validate:* T6 `pragma_weak.c` (symbol binding WEAK, alias defined);
  T1 `pragma_weak_undef.c` (`&ext == 0` at run time, not folded to 1).
  (done: clang's rules in `frontend/c/decl.c`, pending pragmas included, and its
  diagnostics; on Darwin the aliases are local, as clang's.  Also fixed: a
  weak symbol's address, by attribute too, was folded as nonzero in the
  front end and the emitter, and Darwin aliases lacked the `_` on their
  target.  Bindings by the linker in `test/abi/pragma_weak` (T6 does not
  exist), run-time zero in T1 `pragma_weak.c`, warnings in
  `test/diag/pragma_weak.c`.)
- [x] **`#pragma pack(push, ident, N)` ignores N; `pop, ident` pops one** [P].
  (done: `pragma_pack` in frontend/c/token.c with clang's rules: labels, `pop, id`
  to the label (a missing one pops nothing), `pop, N`, `pack(0)` = default,
  invalid values and malformed pragmas ignored whole; tests
  `test/vcc/pragma_pack.c`, `pragma_pack_pop.c`.  clang's
  -Wignored-pragmas warnings are not given.)
  `cc.c:919-927`.
  *Validate:* layout `_Static_assert` cases against clang (all forms:
  `push`, `push,N`, `push,id`, `push,id,N`, `pop`, `pop,id`, `pop,N`, `pack()`,
  `show` ignored).
- [x] **Designated-initializer overrides differ from clang** [P].
  Nested GNU range lists now detach on later writes, preserving independent
  element values and positional continuation (`range_designator_overwrite.c`,
  unchanged GCC `gnu99-init-1.c`).
  (done, frontend/c/init.c: an array element or range a later designator covers is
  dropped, not evaluated; a designator into a sub-object initialized by one
  value keeps the rest of it: a string becomes one element per character,
  a struct/array value one member access / element per part, a union value
  is read again through the member the designator picks.  Left, a clean
  error: the overridden value has side effects (clang evaluates it once).
  Only a run that starts below an earlier one's end scans for covered runs,
  so in-order lists stay linear (`init_large.c`, `vcc-timeout: 10`).
  Tests `test/vcc/desig_override.c`, `desig_override2.c`.)
  `.s = "ab", .s[3] = 'z'` loses "ab"; `.i = iv, .i.y = 99` zeroes `i.x`;
  `[0] = side(), [0] = side()` evaluates both.
  *Validate:* T1 `desig_override.c`, static and automatic variants, with
  side-effect counters (clang's behaviour is the oracle).
- [x] **Header/snapshot cache keys incomplete** [R].  *Done:* `opt_sig` hashes every
  `cc_options` field before `outfd` (cc.h says parse options go there) plus emit;
  `sig_of` adds wargs and error_limit; `bench/vcc-cachekey.py` 6/6 (the decl
  cache rows need bigger headers to reach a restore).  Was:  `opt_sig`
  `driver/declaration-cache.c:356` misses `-std`, `-fgnu89-inline`, `-finstrument-functions`,
  implicit-int/function flags; `sig_of` `pp.c:1518` misses `-W`/`-Werror`.
  Derive both keys from one struct of all parse-affecting options so a new
  option cannot be forgotten.
  *Validate:* T12-style test via `vcc-cbatch.py`: in one process compile
  the same TU first with flags A and then B for each listed option; output
  (object bytes / diagnostics) must equal a fresh process with B.  Add a
  static check: a unit test that toggles every field of the options struct
  and asserts the signature changes.
- [x] **Default `-O2` when no `-O` given** [P].  (done: default -O0; `-Os -O2`
  no longer keeps `__OPTIMIZE_SIZE__`; `bench/vcc-driver.py`, the start of T11,
  checks the -O predefines against clang on both targets.)  `driver/ppdriver.c:426`.  Default
  `opt = 0` (`__NO_INLINE__`, no `__OPTIMIZE__`).
  *Validate:* `vcc-diff.py` predefine check: `vpp -dM -E` (once `-dM`
  exists) vs `clang -dM -E` for `''`, `-O0`, `-O1`, `-O2`, `-Os`, `-Oz`;
  until then T11 with `#ifdef __OPTIMIZE__` probes.
- [x] **Inline asm `%=` depends on thread scheduling** [R].  (done: the
  function's symbol × 1024 + its n-th emitted asm that uses `%=`
  (codegen/asm.c); `test/vcc/asm_uniq.c` with the new `vcc-threads-same`
  marker (`-S` equal with 1 and 8 threads; failed before) and `vcc-noref`
  (clang rejects `%=` on AArch64).)
  `atomic_fetch_add(&E->asmno)` `emit.c:8010`.  Number per function
  (function index × asm index within it).
  *Validate:* T6/`vcc-obj.py`: compile a TU with several `%=` asms in many
  functions with `VCC_THREADS=1` and `=8` ten times; objects byte-identical.
- [x] **LSE-only atomics** [R].  (done: without LSE (Linux default, armv8-a,
  -mcpu=cortex-a53 ..., `+nolse`) every casal/swpal/ld<op>al is an
  ldaxr/stlxr loop (codegen/builtin.c at_cas/at_swp/at_ldop, also the
  `_Atomic` operators); LSE as before for armv8.1-a+, `+lse`, newer -mcpu
  and Darwin; `__ARM_FEATURE_ATOMICS` as clang (`bench/vcc-driver.py`,
  16 rows).  `vcc-ctests.py --cpu cortex-a53` runs vcc's binaries on a CPU
  without LSE: 1729/1729 (the old binary: 7 run failures).  Not done:
  clang's Linux default is outline atomics (`__aarch64_ldadd4_acq_rel`
  calls, -moutline-atomics); vcc inlines the loop as -mno-outline-atomics.)  `emit.c:5190-5330`.  Without `+lse` in
  `-march`, call outline atomics (`__aarch64_ldadd4_acq_rel` etc., as clang's
  `-moutline-atomics` default on Linux) or emit LL/SC loops.
  *Validate:* T6 (no LSE opcodes without `+lse`); T1 run under
  `qemu -cpu cortex-a53` (no LSE) for `atomic-*.c`; extend `vcc-ctests.py`
  with a `--cpu` option.
- [x] **`error()` calls dropped silently when not folded** [P].  (done: every
  surviving call is clang's error "call to 'f' declared with 'error'
  attribute: msg", once per call site (codegen/call.c); the old drop-and-go-on
  rule only with `-fvcc-kernel`, which `-D__KERNEL__` implies.  Test
  `test/diag/attr_error.c` with the new `diag-mode: -c` marker.  Differs:
  clang reports errors inside `static inline` functions after their caller's
  (it emits them later), vcc where they are inlined.  The `warning`
  attribute is still ignored.)
  `emit.c:4329-4347`.  Outside kernel compat mode, a surviving call to a
  function with `__attribute__((error))` must be a diagnostic (clang -O2
  rejects).  Keep the kernel behaviour behind a flag (`-fvcc-kernel`).
  *Validate:* T4 `attr-error.c`; kernel `vcc-obj.py` still 840/840.

## Tier 2: vccd as a safe long-lived service

- [x] **Socket has no peer check, lives in /tmp** [R].  *Done:* default socket
  `$XDG_RUNTIME_DIR/vccd/` or `$TMPDIR/vccd-UID/` (0700, owner and mode checked
  by shim and server, `protocol.h`), socket bound 0600, peer uid checked on
  accept (getpeereid / SO_PEERCRED).  The other-uid test needs sudo.  Was:  `server/vccd.c:924-927`,
  `bin/vcc/shim.c:85-135`.  Socket in a per-user 0700 directory
  (`$XDG_RUNTIME_DIR` or `$TMPDIR/vccd-$UID/`), check the directory's owner
  and mode before connecting (shim) and before binding (server), and check
  the peer uid with `getpeereid`/`SO_PEERCRED` on every connection.
  *Validate:* T12 (mode 0700 dir, 0600 socket; connection from another uid
  refused; pre-created squatting socket/dir detected by the shim and refused).
- [x] **ld.lld spawned with the request's environment** [R].  *Done:* spawn drops
  `DYLD_*`, `LD_PRELOAD`, `LD_LIBRARY_PATH`, `LD_AUDIT` (blacklist, so tools keep
  the variables they read); vccd-test spawned-tool-env-clean.  Was: `server/vccd.c:543`.
  Whitelist (PATH and the few the tools read); drop `LD_*`/`DYLD_*`.
  *Validate:* T12 (`DYLD_INSERT_LIBRARIES=/nonexistent` in the request does
  not change the child; a test helper dumps its env).
- [x] **No protocol/build versioning** [R].  *Done:* magic "roo2", the header
  carries a build id (size+mtime of the vccd next to the shim); vccd answers
  VCCD_STATUS_MISMATCH and the shim says to restart it (vccd-test
  other-build-refused).  No automatic restart.  Was:  `protocol.h:12`.  Add a build
  id (hash of the binary or a build-time constant) to the handshake; on
  mismatch the shim restarts the server or falls back.
  *Validate:* T12 (shim from build A against vccd from build B → clear
  error or restart, never silent service).
- [ ] **Input-driven `abort()` / stack overflow kill the whole server** [P].  (recursion
  done: `include/port/stack-guard.h` checks the stack pointer in cast_expr, the #if evaluator
  and argument pre-expansion; 128 KB left ends the TU (clang itself crashes
  on these inputs).  Overflow is a per-TU error now too: pp's
  token stack via fatal(), as/elf regions via `vjob_fail` (include/obj/obj.h, a longjmp
  the driver sets); vcc-stress 19/19, --vccd 20/20.  Audited the rest: input-driven
  ones are errors now (front-end heap: fatal; labels: efatal; pp shared region,
  identifier table: `vjob_escape` in common/intern.c; ELF sections: vjob_fail); the
  remaining aborts are mmap failures and internal invariants.  No stress case
  yet for the front-end heap, labels or sections.)
  Aborts: `emit.c:1934,2036,2778`; `cc.c:44,1518,6219`; `as/as.c:107`;
  `obj/obj.c:33,40,234`; `pp.c:89,301,1721,2145,2531,2594`; `common/intern.c:96,195`;
  `frontend/lex/lex.c:1318,1394`; `driver/ppdriver.c:65`.  Recursion without depth limit: cc.c
  expression/declarator parser, pp `#if` evaluator and argument
  pre-expansion.  Turn arena/limit failures into per-TU fatal errors
  (longjmp to the job, as `fatal_jb` already does in pp), add depth limits
  matching clang's (bracket depth 256 by default, `-fbracket-depth`), and
  check `vjob_region` reservations in `mk`/`sym`/`tag`.
  *Validate:* T5 (every generated case: exit 1 with a diagnostic, and in
  `--vccd` mode the server keeps serving).
- [x] **Unbounded argv arrays** [P].  *Done:* the directory lists are sized by
  argc (one per-thread allocation), the depfile table grows until every file
  fits; 5000 `-I`, 5000 `-include` and a 70000-header depfile work.  Was:  `driver/ppdriver.c:410,489-498,561`
  (`dirs_i[1024]`, `all[2048]` …), `driver_depfile_write` 65536 files
  (`driver/ppdriver.c:288`).  Allocate from the job arena.
  *Validate:* T5 (5000 `-I`, 5000 `-include`, a TU with 100k headers in the
  depfile).
- [x] **fd handling: uninitialised cmsg, three fds assumed** [R].  *Done:* cbuf
  zeroed, room for 16, count from cmsg_len; not exactly three closes them all.  Was:
  `server/vccd.c:172,186,875-877`.  Zero `cbuf`, take the count from `cmsg_len`,
  reject requests without exactly three fds.
  *Validate:* T12 (send 0, 1, 2, 4 fds; server rejects, does not close
  unrelated fds — check that the listening socket and other open connections
  survive).
- [x] **fds leak into children** [R].  *Done:* received fds get FD_CLOEXEC,
  spawn uses POSIX_SPAWN_CLOEXEC_DEFAULT (closes the macOS race), O_CLOEXEC
  in fixdep/elf/ppdriver.  No child-fd test yet.  Was:  Use `MSG_CMSG_CLOEXEC` (Linux; on
  macOS set `FD_CLOEXEC` immediately under a lock or spawn with
  `POSIX_SPAWN_CLOEXEC_DEFAULT`), `O_CLOEXEC` in `driver/ppdriver.c:743`,
  `utils/fixdep.c:407`.
  *Validate:* T12 (a spawned child lists its open fds: only 0-2).
- [x] **No timeouts; idle connections stall all workers** [R].  *Done:* a worker
  waits 2 s (poll) for the request, SO_RCVTIMEO bounds the payload; a failed
  recvmsg no longer spins in CMSG_NXTHDR on a zero cmsg_len.  Was:
  `server/vccd.c:182,190,305`.  Read the request with a deadline; don't tie a
  worker to a connection until the request is complete.
  *Validate:* T12 (open 2×ncpu idle connections, then a real job completes
  within 1 s).
- [x] **Jobs not cancelled on client exit; SIGTERM exits mid-job** [R].  *Done:* a
  job whose client's connection reads EOF writes no output (checked before
  each write and the rename); the work itself still runs to the end.
  vccd-test killed-client-no-output.  (SIGTERM
  done: the listening socket closes and is unlinked, queued and running jobs
  finish, then exit; vccd-test sigterm-drains.  Cancel on client exit open.)
  `server/vccd.c:979,1017`.  Detect peer close (kqueue EV_EOF / poll POLLHUP) and
  abandon the job; on SIGTERM stop accepting and drain.
  *Validate:* T12 (kill the shim during a long TU: the output file is not
  written; SIGTERM during a job: the job completes or its output is absent).
- [x] **Outputs written in place** [R].  *Done:* every output (`-o`, `-S`, deps)
  goes to `NAME.vccPID.N.tmp` and is renamed when complete; a failed job
  unlinks it and leaves the old file.  Was:  `driver/ppdriver.c:266`.  Write to a
  temporary next to the target and `rename()` (utils/ar.c already does).
  *Validate:* T12 (interrupt during write: either the old file or none,
  never a truncated one with a new mtime).
- [x] **Request environment and umask ignored by in-process jobs** [R].  *Done:*
  jobs read `CPATH` (after -I), `C_INCLUDE_PATH` (after -isystem) and
  `SOURCE_DATE_EPOCH` (UTC __DATE__/__TIME__/__TIMESTAMP__) as clang, from the
  request's env under vccd (`driver_environment.envp`); outputs follow the client's umask.
  vccd-test request-env-reaches-job.  The VCC_*/VPP_* debug knobs stay
  server-wide.
- [x] **Caches never invalidated** [R].  *Done:* a build is a client process
  group (the shim sends getpgrp(); a shell gives each make its own): a request
  from a new group waits for running jobs, then the file, directory and
  include caches are emptied (header/decl caches key on the file entries, so
  they follow).  Within one build nothing is revalidated: a header regenerated
  mid-build after it was read is still stale.  vccd-test: 4 tests XPASS.  Was:  Token cache keyed by path, caches
  misses (`frontend/lex/lex.c:1484-1505`, comment at `pp.c:4025`); dircache; header
  cache replays on top.  Either scope all caches to a build (make's
  jobserver lifetime / an explicit generation the shim sends) or validate
  with (dev, ino, size, mtime_ns) on each first use per build and drop
  dependent header-cache regions.
  *Validate:* T12 (compile, edit a header, compile → new object; create a
  previously missing header → found; `__has_include` flips; regenerate a
  header during a build).
- [ ] **Memory only grows** [R].  (server side done: at a new build, idle, a vccd
  above `VCC_RESTART_MB` resident (6144) answers what it holds VCCD_STATUS_RETRY
  and execs itself on the same listening socket; the shim resends.  vccd-test
  memory-restart-between-builds.  Within one build memory still only grows.
  A full identifier table (6.3M) is an error for the TU, and vccd restarts at
  its next idle moment; vcc-stress idents-7M.)  16 GB shared region aborts, 8 GB header
  cache cap, intern table cap ~6.3M identifiers, bump-only per-thread
  arenas.  Evict header-cache regions (LRU by generation), reset per-job
  arenas, grow the intern table or start a new generation.
  *Validate:* T12 (compile the kernel's 840 TUs 5× in one server; RSS after
  run 5 within 10% of run 2); T5 (an input with 10M distinct identifiers
  gives an error, not an abort).
- [x] **No server → exit 127** [R].  *Done:* on the default socket the shim
  starts vccd itself (flock in the socket directory, setsid, log there;
  `VCC_NO_AUTOSTART` turns it off); vccd-test autostart-default-socket (4
  concurrent shims, one server).  An explicit VCC_SOCK is never started.  Was:  `bin/vcc/shim.c:96-99`.  Autostart the server
  (with a lock) or run the job in-process in the shim.
  *Validate:* T11 (no server running: `vcc -c a.c` succeeds).
- [x] **Request size capped at 1 MB** [R].  *Done:* 8 MB (above Linux's ARG_MAX
  with an 8 MB stack); larger still gets the shim's "request too large";
  vccd-test request-2mb.  Was:  `protocol.h:13`.  Long command
  lines plus env exceed it.  Stream or raise with a clear error.
  *Validate:* T12 (a 2 MB env/argv works or fails with a message).

## Tier 3: features general C needs

### Codegen (`emit.c`)
- [ ] **Windows x64: packed boolean vectors at calls** [C].  clang passes an
  `ext_vector_type` `_Bool` vector of N lanes on the Microsoft convention one
  lane per argument position (results through a hidden pointer, the bits
  packed), as it does for System V; `arch/x64/codegen/abi-win64.c` passes it
  as one by-reference vector.  `src/test/abi/packed_bool`, `packed_bool_4096`
  and `packed_bool_variadic_fixed` are `vcc-xfail-win64`.
  *Validate:* `bench/vcc-abi.py --target win64 packed_bool` XPASSes; remove the markers.
- [ ] **Windows x64: MS bit-field layout of a packed record with `:0`** [C].
  `tcc/95_bitfields_ms` TEST 6 (`__attribute__((packed))`, `int x:12, y:4, :0,
  :4, z:3`): clang starts the unit after `:0` at the byte where the previous
  unit ends, vcc aligns it to the type (`frontend/c/record-layout.c`); found
  by `bench/vcc-win64.py`.
  *Validate:* `bench/vcc-win64.py tcc 95_bitfields_ms` passes (XPASS: remove
  its `XFAIL` entry in `bench/vcc-win64.py`).
- [ ] **x86-64: `naked` functions** [C].  `__attribute__((naked))` is not
  implemented for x86-64: the body gets a frame, so the asm's `retq` sits
  inside it; on Windows the epilogue checker of `vcc-win64.py --seh` flags
  `vcc/dead_results` (`XFAIL`, stage seh; the function is never called).
  *Validate:* `bench/vcc-win64.py --seh vcc dead_results` XPASSes.
- [ ] **Windows x64: `-c` without text for the frame and sections** [P].
  `vpp -c` for the target encodes records directly but hands the `.def`
  records, sections/COMDAT, `.seh_*`, data and strings to the TU's assembler
  as text items (as x86-64 ELF does for its directives); the `coff_section`,
  `elf_reg` + `coff_class`, `coff_seh_*` calls of obj.h could take them
  directly.  *Validate:* `vcc-win64.py --objcmp` stays 2105/2106; emit time
  per TU drops.
- [ ] **Windows x64: `--own` under vccd in a harness** [T].  The shim with
  `--target=x86_64-w64-mingw32` compiles and links (driver/link-mingw.c in
  vccd); `bench/vccd-test.py win64-cross-build-through-the-shim` and
  `bench/zlib/run.py --target win64` (zlib's ./configure and Makefile through
  the shim) cover it since WIN8, but `vcc-win64.py` drives vpp only.
  *Validate:* a `vcc-win64.py --vccd` mode (private vccd, the shim as CC)
  gives the same pass set as `--own`.
- [ ] **Windows x64: the x86 SIMD intrinsic headers** [C].  vcc's own
  `src/headers/x86/` has only what MinGW-w64's `windows.h`/`malloc.h` reach
  (`x86intrin.h`, `emmintrin.h`, `xmmintrin.h`, `mm_malloc.h`: the vector
  types, fences, pause, clflush, MXCSR, rdtsc); no `immintrin.h`, no SSE/AVX
  arithmetic intrinsics, no `cpuid.h`, so `<intrin.h>` (which includes
  `pmmintrin.h` and the rest) does not compile without `-resource-dir`.  The
  WIN8 projects are configured without them (FFmpeg `--disable-asm`).
  *Validate:* `vcc-diff.py --win64` with vcc's headers on the intrinsic TUs
  (`intrin.h`, `-march=haswell`) and a test calling SSE2/AVX2 intrinsics
  under Wine against clang's build.
- [ ] **Windows x64: `<tgmath.h>` and `<stdbit.h>`** [C].  Neither MinGW-w64
  nor `src/headers/freestanding/` has them (clang's resource directory has
  `tgmath.h`; C23's `stdbit.h` neither).  *Validate:* a `src/test/vcc` case per
  header compiled for x86_64-w64-mingw32 without `-resource-dir`.
- [ ] **`-fcommon` and `__attribute__((common))` are ignored** [C].  vcc
  emits a tentative definition as `.bss` (`B`) on every target where clang's
  `-fcommon` or the attribute gives a common symbol (`C`), found in WIN8's
  vnm comparison; a program that defines the same tentative variable in two
  TUs and relies on `-fcommon` fails to link.  *Validate:* `vnm` of a TU with
  both shows `C`, as clang's, for ELF and COFF; a two-TU test links with
  `-fcommon`.
- [ ] **`-coverage` is accepted and ignored** [C].  zlib's Makefile builds
  `infcover` with `-coverage`; clang instruments and links the profile
  runtime, vcc compiles normally (the program runs, no `.gcda`).
  *Validate:* either an error ("unsupported option") or gcov-compatible
  counters; decide which.
- [ ] **Windows x64: FFmpeg with NASM, SQLite's TCL suite** [T].  WIN8 ran
  FFmpeg's C fallbacks only (`--disable-asm`) and SQLite without its TCL
  testrunner (no tclsh for Windows on the host; testfixture.exe needs one).
  *Validate:* `bench/ffmpeg/run.py --target win64` with x86asm (NASM objects
  linked by vlink) and a Windows Tcl to build testfixture.exe.
- [ ] **wasm: the optimizations the gcc torture tests assume** [P].  The
  wasm generator (`arch/wasm/codegen/`) has no inliner or dead-code removal:
  `20020720-1`, `20030216-1`, `20041114-1`, `compare-3`, `pure-1`,
  `shiftopt-1` call a `link_error` that the optimizer must remove, and
  `bcp-1` needs `__builtin_constant_p` of an inlined argument.  They are
  `WASM_XFAIL` in `bench/vcc-wasm.py` (premerge's wasm job).
  *Validate:* `bench/vcc-wasm.py -j 6` reports them as XPASS; remove the entries.
- [ ] **wasm: a signed 32-bit constant widened to 64 bits** [C].
  `long long a(void) { return (long long)(int)0x80ff0102; }` gives
  `i64.const 2164195586` for wasm64 (the front end is right: a
  `_Static_assert` that it is negative holds; arm64 is right).  Found by
  `vir_byte_assembly` (marked `vcc-xfail-wasm`).
  *Validate:* `bench/vcc-wasm.py -j 6 --suite vcc vir_byte_assembly` XPASSes; remove the marker.
- [ ] **wasm: the inline suite** [C].  `bench/vcc-wasm.py` leaves the
  `inline` suite (gcc/llvm inlining and IPA run tests, vendored for VIR) out
  of its default run: in premerge's wasm job `gcc.dg-ipa-ipa-sra-1`,
  `gcc.dg-ipa-pr119318`, `gcc.dg-ipa-pr92497-1`, `gcc.dg-torture-matrix-2`
  and `gcc.dg-torture-matrix-6` differ from clang (only the first on a macOS
  host), not looked into.
  *Validate:* `bench/vcc-wasm.py -j 6 --suite inline` is green in CI; drop the filter.
- [ ] **wasm: 128-bit overflow builtins** [C]. `overflow128` covers standard
  integer operands and destinations independently of `_BitInt`;
  `overflow128_effects` checks evaluation counts and result stores on overflow.
  Both are rejected by the Wasm emitter on the same sources with the
  `1f4c8d2` baseline compiler (code-identical to `b97022e`) and the current
  compiler, on wasm32 and wasm64. Implement full-width add/subtract/multiply
  overflow and mixed signedness without truncating operands. The separate
  `overflow128_bitint` also requires the remaining-target `_BitInt` ABI work.
  All three remain required on Linux; Wasm expected failures are executed and
  must become XPASS when fixed, never skipped.
  *Validate:* `bench/vcc-wasm.py -j 6 --suite vcc --no-cache overflow128`
  and `--wasm32`; remove each marker after its full runtime test passes.
- [ ] **wasm: wide integer bitfields** [C]. `bitfield_int128` and
  `bitfield_int128_static` exercise packed fields with `__int128` base types,
  signed extension and neighboring bits. The Wasm emitter handles these as
  whole memory values before examining the bitfield metadata; it needs
  width-aware extraction, signed extension and masked stores without touching
  adjacent bytes. Both tests fail on the same sources with upstream `2339f69`
  (runtime exits 61 and 1); after the shared static-initializer fix the latter
  advances to exit 8, the signed 66-bit `-1` extraction check. Their explicit
  Wasm expected failures remain executed against Clang and reject XPASS.
  *Validate:* `bench/vcc-wasm.py -j 6 --suite vcc --no-cache bitfield_int128`;
  remove each marker only when its full test passes. Linux coverage is required.
- [ ] **wasm: Linux-correctness coverage gaps** [C]. The Linux milestone
  adds tests for behavior fixed in the x86/ARM emitters but still missing in
  the separate Wasm emitter. All 20 tests below also fail with upstream
  `2339f69`; some fail earlier in its parser, so this is a capability boundary,
  not evidence that their newly reachable Wasm lowering is correct.
  `bench/vcc-wasm.py` records each as an explicit expected failure, executes it
  against clang, and rejects XPASS. They are not skipped.
  - Runtime classification/comparison: `fp_classify_builtin`, `fpclassify`,
    `fp_compare_builtin` (long-double comparisons), `isfpclass`, `signaling_nan`; long-double
    classification in `complex_div_runtime`.
  - Integer/aggregate lowering: `atomic_record` (128-bit atomic value conversion),
    `generic_bitcount` (_BitInt runtime precision), `int128_shift_count`
    (wide shift counts), `deep_conditional` (wide GNU omitted-middle values),
    `transparent_union_redecl` (member calling convention).
  - Expression/call lowering: `gnu_conditional_float` (GNU floating conditional),
    `parenthesized_function` (wasm64 only: nested function-pointer return signature; wasm32 passes on baseline f866a31),
    `void_lvalue` (void expressions leave stack values).
  - Optimization/inlining: `array_constants`, `dead_branch_entries`,
    `optimized_conditions` leave unreachable references; `inline_constants`
    needs argument propagation into `__builtin_constant_p`.
  The actual `fp_class_builtins` regression and Unicode static-symbol collision
  are fixed and must pass without expected-failure markers.
  *Validate:* `bench/vcc-wasm.py -j 6 --suite vcc --no-cache -v` using emcc 7
  and Node 24; every named probe must become XPASS when repaired, then remove
  its corresponding entry and rerun. Also retain the unmarked GCC/TCC gates.
- [ ] **PIC/PIE** [P]: done: `cc_options.pic` from the driver (PIE the hosted
  default, `-fno-pic`), `sym_needs_got` (arch/a64/codegen/value.c) as clang:
  `-fPIC` every default-visibility non-static symbol, `-fPIE` those not
  defined in the TU, hidden/protected direct, calls direct; checked by
  `bench/vcc-pic.py` (GOT relocations vs clang per mode).  Left: T8 (a
  shared library + executable run under qemu), the vDSO.
  *Validate:* T6 (`ADR_GOT_PAGE`/`LD64_GOT_LO12_NC` for extern data under
  `-fPIC`, none for `static`/hidden); T8 link a shared library + an executable
  using it, run under qemu-user; kernel vDSO built by vcc and boot test.
- [ ] **ELF TLS** [P]: done as clang's defaults: local-exec for a symbol
  defined in the TU, initial-exec for the others, TLSDESC under `-fPIC`
  (`I_TLV` with `cond` = model, x0 = the address), `.tdata`/`.tbss`
  "awT"; relocations checked by `bench/vcc-pic.py` (`test/pic/tls.c`),
  run by `test/vcc/tls_static.c` (`test/rt` sets `tpidr_el0`).  `-ftls-model=` and
  `tls_model()` as clang (the attribute over the option, the default if
  stronger; `test/pic/tls_model.c`).  Left: local-dynamic proper (now
  TLSDESC per symbol, clang uses `_TLS_MODULE_BASE_` for local symbols), the
  register choice (always x0/x1, clang allocates).  `-ffunction-sections`/
  `-fdata-sections` done (ELF, `.text.f`/`.data.d`/`.tbss.t`...; static
  locals are named after vcc's symbol, `.bss.x.N`, clang `.bss.f.x`;
  `test/vcc/data_sections.c`).
  *Validate:* T6 (relocs per model); T1 run under qemu with the runtime's
  TLS setup (`test/rt` start.S sets `tpidr_el0`); T3 already has TLV.
- [~] **CFI / `.eh_frame`** (and Darwin compact unwind) [P].
  `-fasynchronous-unwind-tables` default on hosted Linux.  ELF done: the
  printer writes `.cfi_*` (CFA = x29 + 16 after the frame record, the frame
  record and callee-saved registers), `-c` goes through the text and the
  assembler's `.eh_frame` (as `-g`).  *Validated:* `bench/vcc-cfi.py`
  1393/1393: one FDE per function covering it, `.text` unchanged, every
  frame function's last row CFA=W29+16 with W29/W30 saved.  Left: Darwin
  (`__eh_frame` or `__LD,__compact_unwind` in the Mach-O writer), the
  direct object path (-c with unwind tables goes through the text), an
  unwinder run (T3 `unwind.c`: no unwinder in the freestanding qemu runtime).
  *Validate:* T6 (`.eh_frame` present, `llvm-dwarfdump --eh-frame` valid
  for every function); T3 `unwind.c` (`_Unwind_Backtrace` from a deep vcc
  call chain lists every frame; a C++ exception thrown through vcc frames,
  built with clang++ for the C++ part).
- [ ] **DWARF** (`-g`): line tables first, then variables/types.  (Line
  tables done for ELF: `-g` takes `-c` through the printer text and `as_run`;
  a row per statement start (none for blocks/case/labels, none for `{`
  without a prologue, as clang), `.file 0` the main file, a v5 CU with
  low/high pc.  `bench/vcc-dwarf.py`: 1377/1377 verify, `.text` unchanged by
  `-g`, `-c` = clang -c of `-S`; rows clang lacks (136 files) are ifs clang
  -O0 folds.  Left: variables, types, frame base; Mach-O; direct objects.)
  *Validate:* T6 (`llvm-dwarfdump --verify`); `llvm-symbolizer` maps
  addresses to the right lines for `test/vcc`; later a scripted lldb
  session (break at file:line, print local).
- [x] **Register spilling** [P] (done: a third try of the function spills each binary
  operator's left value to a frame slot; test/vcc/deep_expr.c, deep_mixed.c (depth 10..200,
  long/calls/__int128/double/long double), stress expr-right-nest-250/10k): ~18-deep right-nested expression was a
  fatal "out of registers" (`emit.c:2176-2195`).
  *Validate:* T1 `deep_expr.c` (generated: right-nested depth 10..200,
  mixed int/FP/calls); T5 for depth 10k (no crash).
- [x] **`_Float16` on Linux x86-64 and arm64.** Storage, literals,
  rounding, arithmetic/conversions, classification, and half-float HFA calls.
  *Validated:* `test/vcc/float16.c` exhausts all 65,536 encodings;
  `test/abi/float16` checks all clang/vcc caller/callee combinations on
  both Linux targets. Kernel sema 887/887 and preprocessing parity 920/920.
- [x] **`__bf16` on Linux x86-64 and arm64.** Distinct type, two-byte
  bfloat exponent/significand layout, literals/conversions/arithmetic and ABI.
  *Validated:* all 65,536 encodings in `test/vcc/bfloat16.c`, cross-compiler
  `test/abi/bfloat16` calls on both Linux targets, invalid-complex diagnostic,
  unity/nonunity builds and kernel sema 887/887 (run and batch).
- [ ] **FP vectors** (`float32x4_t`,
  `emit.c:5886`), **`"w"` asm operands** (`emit.c:7831`), **16-byte atomics**
  (`emit.c:5205`), **128-bit overflow builtins** (`emit.c:4925`),
  **`__atomic_fetch_nand`**, **HVA** (`hfa_walk` `emit.c:2526`).
  *Validate:* one T1 file each (`neon-float.c`, `asm-w.c`,
  `atomic128.c`, `ovf128.c`); T2 for HVA/half HFA.
- [ ] **Stack protector, BTI/PAC (`-mbranch-protection`), `-pg`,
  `-fpatchable-function-entry`**; `-finstrument-functions` is parsed but
  unused in emit.
  *Validate:* T6 (canary load/check, `paciasp`/`bti c`, `__fentry__`
  call, nops + `__patchable_function_entries`); T1 for a canary-smash
  detection under qemu.
- [x] **Fixed limits** (done: call arguments (42ecfb3), nested loops/switches
  and cleanups: reserved stacks (4M), no fixed array; the loop pushes had no
  bound check: test/vcc `limits.c`, 300 arguments, 1000 nested loops, 300
  nested switches, 1000 cleanups, also under ASan.)  Kept by design, with a
  clean error: 16M instructions / 4M labels per function.
- [x] **`-S` on Darwin writes ELF directives** (`.type @function`).  (Done: codegen/darwin.c
  rewrites the text into Apple's dialect; far b.cond/cbz in text are long form on both
  targets.  `bench/vcc-darwin-s.py`: 1428 vcc + gcc-torture tests, -S assembled by `as`,
  same result as the direct object.)
  *Validate:* T3: `vpp -S` output assembled by `as` (Apple) for the vcc
  suite.

### Front end (`cc.c`)
- [ ] **Runtime-behavior warnings by reachability** (clang's DiagRuntimeBehavior): in a
  function body clang defers `-Wconstant-conversion`, `-Warray-bounds`, shift and division
  warnings and drops those in code its CFG finds unreachable.  vcc approximates with
  `c->dead` (constant `if`/`?:`), so it still warns after `return`/`break`, in `while (0)`
  bodies and under `v && 0`.  Queue them per function and filter by the flow walk of
  `-Wreturn-type` (frontend/c/func.c), which would mark reached statements.
  *Validate:* test/diag case with each form; `vcc-sema.py warn` stays 879/879.
- [ ] **C23 / per-TU `-std`** [P] (partly done: per-TU keyword map `kwmap` (C23 spellings ->
  GNU keywords; true/false/nullptr/constexpr), `#if true`, digit separators through a
  translated copy (lex_file_digit_separators_remove: `-E` and `#x` print them without the `'`, clang keeps them),
  test/vcc/c23_keywords.c, c99_ids.c; C23 `auto` type inference and strict `-std=c11` without
  `typeof`/`asm` keywords (D5.5, test/diag/c23_auto.c).  Left: `u8'a'` (one token only in C23: the lexer
  cache needs a per-mode entry, which would also give exact separators), nullptr_t,
  constexpr checks, `_BitInt`, `#embed`): one global keyword table (`kwclass`
  `cc.c:740-768`), `o->std` never read.  Per-TU keyword set; then `bool`,
  `true`/`false`, `nullptr`, `constexpr`, `typeof_unqual`, single-argument
  `static_assert`, digit separators (lexer), `enum : type`, `_BitInt`,
  `= {}`, `[[...]]` standard attributes, `#embed` (pp).
  *Validate:* T1 `c23-*.c` with `vcc-flags: -std=c23`; T4 that the same
  keywords are identifiers under `-std=c99` (`int bool = 1;`).
- [ ] **`_BitInt` remaining operations and targets**: Linux x86-64 and arm64
  now support scalar widths above 128 through limb objects, while preserving
  the existing 1–128-bit paths. Wider arithmetic, integer conversions,
  comparisons, bit-fields, generic counts, constants and replay are covered
  by independent expected values. The declared maximum width is 8388608;
  runtime validation covers selected widths through 7001, not every width.
  x86 wide ABI checks use Clang cross-calls. Clang 23 rejects native ARM types
  above 128 bits, so ARM wide ABI checks use explicitly documented ordinary-C
  aggregate proxies; they are not native Clang wide-integer ABI verification.
  Linux wide integer/floating conversions use full-width magnitude and one
  nearest-even rounding, or exponent/significand decoding with truncation
  toward zero. Exact IEEE probes cover 129, 193, 310, 500 and 7001 bits in
  six x86 formats and five ARM formats; dynamic rounding is not implemented.
  Vector elements wider than 128 bits and switch bounds outside signed
  128-bit representation remain explicitly unsupported.
  Runtime support on other targets remains incomplete. The dirty-padding
  compound-assignment regression stays a verified Wasm expected failure;
  Wasm overflow builtins above 64 bits remain explicitly diagnosed.
  *Validate:* `bench/vcc-bitint.py`, `vcc-bitint-constants.py`,
  `vcc-bitint-abi.py`, `vcc-bitint-replay.py`, `vcc-bitint-float.py`, existing
  `test/vcc/bitint_constants.c`, `bitint_runtime.c`,
  `bitint_compound_padding.c` and `test/abi/bitint/`.
- [ ] **Strict floating environment for wide integer casts**: Linux wide
  integer-to-float lowering currently constructs nearest-even bits and does
  not raise inexact/overflow flags. Accepting `-frounding-math` or
  `-ffp-model=strict` only disables selected fast-math transforms; it does
  not establish dynamic-rounding or exception support. Do not claim strict
  floating-environment conformance for these casts.
  *Validate:* convert volatile unsigned `_BitInt(193)` `(1 << 53) + 1` to
  double under all four `fesetround` modes and check `fetestexcept`:
  upward must produce `0x1.0000000000001p53`, while nearest/downward/toward-zero
  produce `0x1p53`, all with inexact raised. Also test signed values and
  destination overflow. The current default-mode Clang 23 cast likewise
  ignores the environment; its `-frounding-math` wide cast fails in the
  backend, so use exact IEEE expectations rather than that failed oracle.
- [ ] **Builtins**: ~~`__builtin_shufflevector`, `__builtin_convertvector`~~ (done:
  test/vcc/vector_builtins.c; vector diagnostics spell clang's unnamed form, without the
  `(vector of N 'T' values)` note and typedef names) [P].  Library builtins (clang's `F`
  ones, 479) are declared from frontend/c/libbuiltins.inc (gen-libbuiltins.py) as calls to the
  function: `bench/vcc-builtins.py` 473 -> 302 -> 190 -> 160 unknown of 924 for aarch64 Linux;
  `__has_builtin` is also 1 for what the front end implements (pp_builtin_known, set by
  cc): 337 -> 237 -> 153 -> 126 answer 0, none 1 where clang says 0 (the harness fails on that).  Audit the full list against clang's `Builtins.td` for the target
  (generate a list, try each).
  *Validate:* T1 per builtin; `bench/vcc-builtins.py` that compiles a
  generated use of each clang builtin and reports unknowns.
  (Partly: `__builtin_clzg`/`ctzg`/`popcountg`, `__builtin_ldexp*`,
  `scalbn`, `trunc*`, `round*`, `floorf`/`ceilf`, `fmaxf`/`fminf` added for
  `make -C src self-check`; the generic forms reject a fallback whose
  argument has side effects.  Sized `__sync_*_N`, `__sync_swap`, `__c11_atomic_*`, `__builtin_rotate{left,right}N` and the ACLE `__builtin_arm_{nop,yield,wfe,wfi,sev,sevl,clrex,isb,dmb,dsb,trap,clz,clz64,rbit,rbit64,cls,cls64}`: test/vcc/builtin_rotate_arm.c.  Atomic nand/max/min (`__atomic_*`, `__c11_atomic_*`, `__sync_*` with clang's -Wsync-fetch-and-nand-semantics-changed: diag `sync_nand.c`; LSE ld{s,u}{max,min}al, LL/SC without LSE, nand as a loop), `__atomic_test_and_set`/`clear`, `__c11_atomic_compare_exchange_{strong,weak}`, `__c11_atomic_is_lock_free`: test/vcc/atomic_nand_minmax.c (also `--cpu cortex-a53`, wasm).  Left in that group: floating `__atomic_fetch_{max,min}` and `fmaximum`/`fminimum`, `uinc`/`udec`.)
  ~~`__has_builtin` answers 0 for names absent from the kernel/SDK~~ (done for what vcc
  implements, see above; the rest are builtins vcc lacks).
- [ ] **SVE in the front end**: baseline AArch64 builtin sizeless types and
  libc declarations work; invalid size, array, field, storage and pointer
  arithmetic uses are diagnosed. Four diagnostics and declaration runtime
  checks pass on Linux x86/ARM; ARM c-testsuite 220/220.
  Done (2026-11-12): sizeless values in the arm64 direct emitter and a
  vcc-owned `include/arm_sve.h` for what PostgreSQL's
  `pg_popcount_aarch64.c` uses (`svptrue_b*`, `svcnt[bhwd]`, `svdup_u8/u64`,
  `svld1_u8/u64`, `svadd/svand[_n]_u8/u64_x`, `svcnt_u8/u64_x`,
  `svaddv_u8/u64`, `svwhilelt_b8_s32/s64`), each a macro around inline asm.
  A sizeless object is a frame slot of the architectural maximum (256 bytes,
  a predicate 32); a copy moves the bytes of this vector length through
  z31/p15 (`ldr`/`str`); an asm operand "w" is loaded into z16+ and "Upa"/
  "Upl" into p0+ around the template (`arch/a64/codegen/asm.c`).  `?:` on two
  values of one sizeless type is accepted.  VIR leaves such functions to the
  direct emitter (inline asm).  `bench/pg/speed.py` on a clang-configured
  build: 1574/1574.
  Left: the rest of `arm_sve.h` (tuples, the other element types, `_m`/`_z`
  forms, operators on sizeless vectors, which sema rejects), sizeless
  arguments and results (the vector PCS: diagnosed "code generation for an
  SVE argument or result is not supported yet", so no intrinsic can be a
  function), `target("+sve")` validation (a call to an intrinsic outside
  such a function is not diagnosed), values in registers across statements
  (every intrinsic loads and stores its operands), the `__mfp8` builtin type
  (not needed once the header is vcc's own), and the unbraced
  `ld1d z0.d, p0/z, [x0]` spelling in vas (GNU as takes it; the header
  writes `{z0.d}`).
  *Validate:* test/vcc/sve_popcount.c under `qemu -cpu max` against clang
  (the runtime's start.S sets CPACR_EL1.ZEN); test/diag/sve_*.c; layout
  checks for the types.
- [x] **`ext_vector_type` silently dropped; unknown attributes no warning** [P].
  Done: `ext_vector_type` (test/vcc/ext_vector.c);
  `-Wunknown-attributes` for GNU syntax from clang's attribute names per
  target (`tools/gen-attrnames.sh` -> `attrnames.inc`) with its typo
  correction (SimpleTypoCorrection), 625 generated typos identical to clang
  on all three targets, diag `unknown_attribute.c`; `[[x]]`, `[[gnu::x]]`,
  `[[clang::x]]` (`_Clang`, `__gnu__`) and other vendors from
  `__has_c_attribute` (`unknown_std_attribute.c`, the 625 again per scope).
- [x] **`[[__clang__::x]]`**: (done: clang's -Wignored-attributes warning with
  the `_Clang` fix-it, taken as `clang::`; test/vcc `attr_clang_macro.c`;
  the predefine tables in clang's -dD order, so the "expanded from macro"
  note says `<built-in>:2` as clang: diag `attr_clang_builtin_line.c`.)
  Only `__clang__` written in the source, after a `[[` written there, as
  clang: through a macro (`#define A [[__clang__::x]]`, `[[S::x]]`,
  `B(__clang__)`) it is 1 and the error is clang's "expected ']'" (not
  "expected attribute name").  The `]` fix-it line and clang's later errors there belong to
  the error recovery item.)
- [ ] **General object-size provenance** [P]. The Linux milestone on base
  `9d022c3` handles strict-flex levels 0–3, declared subobjects without padding,
  minimum mode 3, stable `counted_by` pointer fields (dynamic modes 0–2), and
  direct immutable inline pointer parameters. Volatile pointer/base expressions
  stay unknown without reads. Five unchanged GCC cases (`pr101836_1/2/3`,
  `pr39343`, `pointer-counted-by-8`) and permanent regressions cover this scope.
  Still missing: general control-flow/alias provenance, transitive inline
  bindings, `alloc_size` inference, and dynamic side-effecting expressions.
  Wasm inline argument-bound propagation remains unsupported: the unchanged
  `object_size_inline` regression gives identical assembly and runtime failure
  with main `9d022c3` and this milestone on wasm32/64; Clang passes. Its
  expected-failure marker executes the test and rejects XPASS.
  Do not infer a whole allocation from a known subobject or treat unknown as
  known. Full S4 and non-Linux targets remain separate gates.
  *Validate:* unity and file-by-file builds; own x86/ARM runtime suites;
  `vcc-gcc-dg.py` on those five sources; strict-flex 0–3, volatile/null-base and
  raw allocation probes; kernel `vcc-sema.py run` and `batch` (887 TUs).
- [x] **Attributes without effect**: none left.  Accepted
  and needing nothing today: `returns_twice` (locals live in memory:
  `test/vcc/returns_twice.c`; the optimizer item must honour it when
  locals go to registers), `alloc_size` (only `__builtin_object_size`,
  where vcc's -1 is the conservative answer), `target` (vas gates no
  instructions yet: the `.arch` item).  (Done: `format(printf, i, j)`
  (`-Wformat`, `-Wformat-extra-args`, `-Wformat-insufficient-args`,
  `-Wformat-invalid-specifier`, `-Wformat-security`; `frontend/c/fmt.c` after
  clang's CheckFormatHandler, the type answers per target from clang in
  `frontend/c/fmttab.inc` by `gen-fmttab.py`; Linux and Darwin, not wasm or
  `-fshort-wchar`; diag `format*.c`, 16 random 300-call files identical to
  clang on both targets except typedef names, kernel warn 879/879,
  PostgreSQL 0 differences); `nonnull` (`-Wnonnull`
  for null constants, plain/indexed/on a parameter: diag `nonnull.c`);
  `ifunc`
  (STT_GNU_IFUNC, global as clang, direct under PIE like an alias:
  `test/pic/ifunc.c`, `alias.c` in vcc-pic.py, which now also compares
  symbol types and bindings); `deprecated`
  (functions, variables, typedefs, tags, enumerators; not inside a
  deprecated function) and `warn_unused_result`/`[[nodiscard]]`, also in
  statement expressions: diag `deprecated*.c`; kernel warn 879/879,
  PostgreSQL's 1574 files no new warnings.)
  *Validate:* T6 for `ifunc` (STT_GNU_IFUNC); diag tests
  for `format`/`nonnull`/`deprecated`/`warn_unused_result` warnings.
- [ ] **Blocks (`^{}`)** for Darwin headers.
  *Validate:* T3 compile programs including `<dispatch/dispatch.h>` and
  calling a block.
- [x] **C90 digraph and statement scope semantics** [P]. Strict C89/C90
  disables digraphs; C94 and GNU89 enable them. Implicit selection/loop
  scopes start with C99. Lexer/header-cache modes, paste, stdin and `-D`
  are checked by `bench/vcc-c90.py`, including 21 mixed-mode jobs.
- [x] **Scope details**: (done: a declaration after a label, `case` or
  `default` (C23; -Wc23-extensions before), also after `__attribute__`s;
  a prototype definition's parameter-list tags are visible in its body, K&R's
  not (clang); -Wvisibility, -Wdeprecated-non-prototype (after the body);
  "variable has incomplete type 'T'" with the forward-declaration note, also
  with an initializer, the variable then invalid (no follow-on member error);
  "array has incomplete element type"; "previous definition is here" for a
  defined function: test/vcc `label_decl.c`, diag `label_decl_kr_vis.c`,
  `incomplete_var.c`, `conflict_prev_def.c`.)
  Labeled declaration groups remain in their enclosing lexical scope;
  stack lifetime and cleanup are covered by `label_declaration_lifetime.c`
  and unchanged Kefir `std_attributes18`.
- [ ] **Fixed limits** (struct members now 1M; flat declarator operators use
  growing arena buffers with a 1M resource budget; genuinely nested declarators
  remain stack-guarded). Flat labels parse iteratively into flat AST lists;
  large switch duplicate checks use a sorted interval sweep. GCC's unchanged
  10,000-pointer and 100,000-case tests compile on Linux x86-64 and ARM64.
  Remaining limits include 16 nested anonymous members (`find_member`) and
  256 K&R parameters.
  *Validate:* `flat_declarators_labels.c`, stress `pointer-10k` and
  `case-labels-*`; GCC `limits-pointer.c` and `limits-caselabels.c`;
  excessive pointer storage and genuine expression nesting fail cleanly.

### Preprocessor and lexer (`pp.c`, `frontend/lex/lex.c`)
- [x] **`#pragma once` by path string, capped at 4096** [P].  (Done: by (dev, ino) from
  read_src, list grows; symlink/hard link/`a/../a` checked against clang -E -P;
  test/vcc/once.c, once_tri.c.)
  `pp.c:4189,5191,4479`.  Key on (dev, ino); unbounded set.
  *Validate:* `src/test/fuzz/regress/pp/` style files + `make -C src fuzz-diff`
  extended with a directory fixture (symlink, hardlink, `a/../a/x.h`),
  compared with `clang -E -P`.
- [x] **UTF-8 BOM not skipped** [P].  *Done:* read_file blanks a leading BOM
  (columns kept); test/vcc/bom.c with bom_inc.h.  (lex_file_get_hook inputs,
  fuzz-pp, are not filtered.)
- [x] **Trigraphs** under strict `-std=c89/c99/c11/c17` or `-trigraphs` [P].  (Done:
  `lex_file_trigraphs_replace` lexes a translated copy; `-ftrigraphs`/`-fno-trigraphs` too; test/vcc/trigraphs.c.
  No `-Wtrigraphs` warnings yet.)
- [x] **No `//` comments in C89** (`-ansi`, `-std=c89`): clang lexes `//` as two `/` there [P].
  (Done: `lex_file_c89_convert` copy, strict c89 only as clang; test/vcc/c89_comments.c.)
  *Validate:* pp-diff fixtures with each `-std`.
- [x] **`SOURCE_DATE_EPOCH`; `__TIMESTAMP__` from file mtime** [P].  (Both done:
  __TIMESTAMP__ is the current file's mtime, `??? ...` when stat fails, as clang.)
  `pp.c:3168-3178`.  *Validate:* T11 (fixed epoch → exact strings).
- [x] **`\U` outside the BMP printed unconverted in `-E`** [P].  (Done: identifiers' UCNs
  print as UTF-8 (not in asm mode, not in literals); `##` accepts UCNs; regress/pp/ucn-ident.c.)
  *Validate:* pp-diff fixture.
- [x] **`#include` in macro args accepted** [P].  (Done: clang's error and
  "expansion of macro" note, diag `include_in_macro_args.c`.  `-E` output on errors
  done: printed to stdout with status 1, no file with `-o`, as clang;
  checked by the error row of `bench/vcc-driver.py`.)
  *Validate:* T4.
- [x] **`#embed` / `__has_embed`** [P].  (done: `#embed "x"` with limit,
  clang::offset, prefix, suffix, if_empty, `__has_embed` 0/1/2, -Wc23-extensions,
  -M lists the file, clang's search (absolute, or for quoted names the
  includer's directory followed by the working directory);
  test/vcc `embed.c` checksums the file embedding itself; `--embed-dir` (`<x>`
  only there, `"x"` after the includer and working directories); `-E`/`-E -P` placement of
  the token after the data, byte for byte as clang; limit() takes a constant
  expression; a header with #embed is not cached (replay would not see a
  changed data file).)
  *Validate:* `bench/vcc-embed-search.py`: 40 Clang comparisons on both
  Linux targets, including search precedence, queries and dependencies;
  unchanged Kefir `preprocessor_embed7` in all four compiler combinations.
- [ ] **Output modes**: `-CC` (comments in macro definitions and arguments,
  `//` as `/* */`) and `-traditional-cpp` are rejected with an error.
  *Validate:* `bench/vcc-diff.py text` (bytes, not tokens) for `-E` with and
  without `-P`.  (`-E -P` text done: clang's spacing (leading space,
  AvoidConcat), indentation to the expansion column, MoveToLine's blank lines
  and file-change newlines, `#line` as a file change, assembler `#` lines
  re-spaced as tokens; a header guard counts once the file is complete, as for
  `-H`.  `-C` done: comments printed as clang's comment tokens, dropped in
  directives, skipped groups and macro arguments; vccd accepts it, so
  kbuild's `cpp_lds_S` (vdso.lds) no longer needs clang.  Line markers
  (`-E` without `-P`) done: GNU `# N "file" 1/2/3/3 4` as clang's
  FileChanged/WriteLineInfo, `<built-in>`/`<command line>` (its line: the
  predefined macros + 3), `./` for `-include` from the current directory,
  extern "C" system directories (`/usr/include`, the SDK's), `#line`, up to
  8 newlines before a marker; `bench/vcc-diff.py lines` 912/912.  The
  predefined macros now match clang's `-dM` outside the kernel's flags too
  (Linux PIE/CFI defaults, C23 char8_t/%B, Darwin -fshort-wchar and
  -mgeneral-regs-only).
  `bench/vcc-diff.py text` and `textc` (-C) 912/912,
  `src/test/fuzz/pp-diff.py --text` on regress/pp and the seed corpus.)  (`-M`/`-MM` done: see Depfiles;
  `-H` done: `bench/vcc-diff.py hdr` 912/912, header cache off under `-H`;
  `-dD` done: `bench/vcc-diff.py dd` 910/912, predefined macros in clang's
  order from `predef_order_*` (gen-ppdata-order.py); the two left are the
  vDSO's cpp runs, which clang gives `__PIC__`/`__PIE__` and the CFI macro
  (no -fno-PIE there): PIC/PIE item.)
  (`-dM` done: `bench/vcc-diff.py dm` is byte-identical to clang on all 879
  C TUs.  Open: on kernel `.S` files clang's `-dM` leaves out macros defined
  after a point in the headers (strchr.S: all of linux/linkage.h onwards)
  although `#ifdef` sees them; not understood, so `dm` skips `.S`.)
- [x] **Depfiles**: `-MMD` lists system headers, `-MD` without `-MF`, default
  target `-`, `-MP`, `-MQ` [P].  (Done: clang's rule format (75-column
  wrapping), targets and default file names, `-M`/`-MM`, `-MMD` without system
  headers (an `-isystem` with no `-I` was not a system directory), headers
  skipped by guard or `#pragma once` and `__has_include` hits listed under
  each spelling, Darwin's `SDKSettings.json`; `bench/vcc-diff.py deps`
  912/912 byte-identical.)
  *Validate:* T11 against clang's `.d` files for each flag combination
  (byte comparison after path normalisation).
- [x] **Options**: `-imacros`, `-isysroot`/`--sysroot`, `-nostdlibinc`,
  `-iprefix`/`-iwithprefix`, `-mmacosx-version-min`, unknown `-std=` values
  (error), `iso9899:199409`.
  *Validate:* T11 rows; predefine diff (`-dM`) for `-std` and
  `-mmacosx-version-min`.  (Done: `-imacros` as clang's `#__include_macros`
  + `##` in `<built-in>`, before every `-include`, its markers too; the
  `-std=` table is clang's exact spellings, anything else an error;
  `iso9899:199409` and `c2y`/`gnu2y` versions; the macOS deployment version
  from `-mmacosx-version-min` or a versioned `-macosNN` target (which wins);
  `-isysroot`/`--sysroot`/`-nostdlibinc`/`-iprefix` already matched.
  `bench/vcc-driver.py` rows, -E and -dM byte for byte.)
- [ ] **Predefines for other targets and flags**: only two tables;
  `--target=x86_64`, `-m32`, `-funsigned-char`, `-fstack-protector*`,
  `-ffast-math`, `-march=+sve`, `-pthread`, `-fsanitize=*`, `-fexceptions`
  change nothing.  (Done: `-f[no-][un]signed-char` (macro, and `char`'s
  signedness in pp and cc), `-ffast-math`/`-ffinite-math-only`/
  `-f[no-]math-errno`/`-fno-fast-math` with clang's interplay, `-pthread`;
  checked by `bench/vcc-driver.py` rows.  Left out on purpose until vcc
  implements them, so the macros don't promise what isn't there:
  `-fstack-protector*` (`__SSP*__`), `-fsanitize=*`, `-fexceptions`,
  `-fopenmp`, `-fblocks`, `-mbranch-protection`, `+sve`.)  Generate predefines from (target, flags) using a table
  extracted from clang per flag delta (`gen-ppdata.sh` extended).
  *Validate:* `bench/vcc-predef.py`: `vpp -dM -E` vs `clang -dM -E` for a
  matrix of targets × flags.
- [ ] **Hard-coded paths**: Homebrew LLVM 23.1.4 resource dir
  (`ppdata-predef.inc:921`), CommandLineTools SDK (`:922`), multiarch dir
  (`driver/ppdriver.c:582`).  Discover at run time (`xcrun --show-sdk-path` once,
  cached; resource dir relative to the vcc binary).
  *Validate:* T11 on a machine/CI image with only Xcode, and after moving
  the install prefix.
- [x] **Files ≥ 2 GB reported as missing** (*Done:* the main file says "file too
  large (2 GB or more)"; an #include of one still says not found) (`frontend/lex/lex.c:1373`, `uint32` offsets
  `include/frontend/lex/lex.h:50,61`).  Clear error.
  *Validate:* T5 (sparse 3 GB file → "file too large").
- [ ] **x86-64 host build** (builds and runs since 2026-11-10, PLAN-x86.md X0):
  `classify` and the three searches in `frontend/lex/lex.c` have a scalar version
  for hosts without NEON, CRC32C selects SSE4.2 or software at runtime
  (`include/port/crc32c.h`), `pause` for `yield`, baseline `-march=x86-64`,
  the fuzz targets use the host's clang
  and libFuzzer. AVX2/AVX-512 classification and AVX2 searches added
  2026-11-13 (PLAN-x86.md X0), selected at runtime with scalar fallback. Left: full kernel
  parity runs on this host.
  *Validate:* `fuzz-regress`; `fuzz-lex` (window path vs reference) ~1M
  runs clean; `-S`/`-c`/`-E` of the C suites byte-identical between the
  x86-64 build and an arm64 cross build under qemu-aarch64 (1903 files x 4).
  Still to do: `vcc-diff.py cmp` once a kernel tree is here.

### Assembler (`as/as.c`, `arch/a64/as/enc.c`)
- [x] **`.cfi_*`** → `.eh_frame` [P]: every directive but personality/LSDA
  and `.debug_frame` (an error), CIEs per key, FDEs sorted by CIE key as MC.
  *Validated:* `vas-obj` `cfi-basic.s`, `cfi-all.s`; `bench/vas-corpus.py`.
- [x] **`.loc`, `.file N`, DWARF line tables, `.uleb128`/`.sleb128`** [P]:
  the v5 `.debug_line` (+ `.debug_line_str`) as MCDwarfLineTable writes it,
  rows on the next instruction or data, one sequence per section; gas-style
  `.file 1 "dir/x.c"` splits the path, directory 0 is the cwd.  `.uleb128`
  of a label difference not known yet, `.prefalign` and an alignment after
  one are variable-size items at the end of a split frag (`elf_split`),
  relaxed as MC at the end.  Left: rows in a subsection > 0 out of address
  order, `-fdebug-compilation-dir`.
  *Validated:* `vas-obj` `dwarf-line.s`, `leb128.s`; T7 `-O "0 -g"` 1516/1516.
- [x] **Scalar FP** [P]: 2/1/3-source arithmetic, frint*, fcmp(e) (also #0.0),
  fccmp(e), fcsel, fcvt between S/D/H, fcvt[nampz][su] and [su]cvtf (also
  fixed point), `fmov #imm` and the H moves.  *Validated:*
  `src/test/vas/fp-scalar.s` vs llvm-mc (`bench/vas-enc.py` 293/293);
  `bench/vas-corpus.py -O2` 1441/1519, the rest AdvSIMD.
- [ ] **AdvSIMD** [P]: done: integer 3-same (mul/mla/mls also by element,
  sshl/ushl, smax/smin, sabd/uabd), compare with #0, neg/abs, FP 3-same and
  by element, FP compare (#0.0 too), FP 2-reg misc (frint*, conversions,
  also the scalar `scvtf d0, d0` forms), `fmov` vector immediates, long/wide
  (s/u mull/mlal/mlsl/abdl/abal/addl/subl/addw/subw and the 2 forms),
  shll/sxtl/uxtl, xtn/sqxtn/uqxtn/sqxtun, shrn/rshrn, usra/ssra/urshr/srshr/
  ursra/srsra, uaddlv/saddlv, mvni, `movi d`, ld2-ld4/st2-st4 and lanes,
  ldpsw (`src/test/vas/simd-ops.s` vs llvm-mc).  Left: FP16 vectors,
  saturating arithmetic (sqadd, sqdmull, ...), pairwise FP, crypto beyond
  AES/SHA, by-element forms of the long ops, ld1r variants.  *Validate:*
  `bench/vas-enc.py`, `bench/vas-corpus.py`.
- [x] **Integer aliases** sxtw, uxtb, sxt*/uxt*, cls, ngc, mneg, smnegl,
  CSSC [P].  *Validate:* T7 tables.  (Done: sxtb/sxth/sxtw, uxtb/uxth/uxtw
  (X forms as llvm-mc), cls, rev64 X, ngc(s), mneg, smnegl/umnegl,
  smsubl/umsubl, bfc, CSSC abs/cnt/ctz and smax/smin/umax/umin (register and
  immediate; the vector cnt/umax/umin unchanged); `src/test/vas/int-alias.s`,
  `bench/vas-enc.py` 128/128 vs llvm-mc.)
- [x] **Pointer auth, barriers/hints** (paciasp … xpaclri, csdb, esb, ssbb,
  pssbb, dgh, `psb csync`, wfet, chkfeat, sysl, prfum, ldapur, stlur), **MTE
  irg/addg** [P].  *Validate:* T7 tables.  (Done: PAC hints and register forms,
  blraa…brabz, retaa/eretaa, ldraa/ldrab, csdb/esb/ssbb/pssbb/dgh, psb/tsb
  csync, wfet/wfit, chkfeat, sysl, prfum, ldapur*/stlur*, irg/gmi/addg/subg/
  subp(s)/cmpp/stgp; `src/test/vas/pac-hint.s`, `bench/vas-enc.py`
  vs llvm-mc.)
- [ ] **SVE/SVE2/SME** [P].  Done (`arch/a64/as/sve.inc`): `p/z`, `p/m`,
  Z lists, patterns, `mul #n`; the forms clang generates: predicated and
  unpredicated integer arithmetic, shifts, logic and immediates (dup/dupm/
  cpy/`mov` aliases), compares, predicate logic and permutes, ptrue/ptest/
  while*, cnt/inc/dec, addvl/addpl, index, reductions, last[ab], int<->fp
  converts, ld1/ld1r/ldN/st1/stN contiguous (scalar+imm, scalar+scalar),
  SVE2 xar/eor3/bcax/bsl/usra; FP arithmetic, fused, unary, converts,
  compares, reductions, fadda, fmov/fdup/fcpy, dup indexed; splice, unpk,
  brk*, cntp/incp/decp; gathers/scatters (vector+imm, scalar+vector with
  uxtw/sxtw/lsl), ldff1/ldnf1/ldnt1/stnt1/ld1rq, prf[bhwd]; by-element
  fmla/fmls/fmul/mla/mls/mul, sdot/udot.  Left: SVE2 non-temporal gathers,
  the rest of SVE2 (widening/narrowing, histogram, crypto), SME.  *Validated:*
  `src/test/vas/sve.s` vs llvm-mc; T7 `-O "3 -march=armv9-a+sve2"`
  1519/1519.
- [ ] **Directives**: left: `.arch`/`.cpu` gating instructions (T4).
  (`.eqv`, `.nops`, `.inst.n` are GNU-only: clang rejects them.)
  *Validate:* `bench/vas-obj.py` (`src/test/vas-obj/`: clang -c and
  vasm byte for byte).  (Done: COMDAT/`"G"` groups (SHT_GROUP, signature
  in the group section when undefined), `.addrsig`/`.addrsig_sym`,
  `.altmacro` (`%expr`, `<text>`, bare parameter names), `.type`
  `@gnu_unique_object`/`@common` (OS/ABI GNU as MC), `.comm`/`.lcomm`,
  `.symver` (@@@, remove), `.weakref`, `.float`/`.double`/`.dc.*`,
  `.base64`, `.balignw`/`.p2alignl`, `.end`, `.ident`, `.print`/`.warning`,
  `.variant_pcs`, `.tlsdesccall`, `.reloc` at `.`/labels with
  NONE/TLSDESC_CALL/BFD_RELOC_*; code sections at least 4-aligned.)
- [x] **Relocation operators**: `:tlsdesc:`, `:tlsdesc_lo12:`, `:gottprel*:`,
  `:tprel_*:`, `:dtprel_*:` (add, movz/movk, ldst of every size), `:prel_g*:`,
  `ldr x0, :got:v` / `:gottprel:`, `:pg_hi21_nc:`; TLS relocations make the
  symbol STT_TLS and labels in TLS sections are STT_TLS, as MC.  *Validated:*
  `src/test/vas-obj/tlsops.s`, `tlslocal.s` byte-identical with clang -c.
- [~] **Mach-O syntax** `@PAGE`/`@PAGEOFF`/`@GOTPAGE…`/`@TLVPPAGE…`: `vpp -c x.s`
  for a Darwin target reads Apple's dialect (`macho_dialect_translate`, the inverse of
  emit's `darwin_text`: sections, `L` labels, `.zerofill`/`.tbss`/`.comm` log2
  alignment, NEON `op.T vA, vB` and `mov d0, v1[1]`) into the Mach-O writer
  (commons now as Mach-O commons).  *Validated:* `vcc-darwin-s.py`: vpp -c of
  vcc's -S is byte-identical to the direct object (1447 tests; not
  branch_far.c, whose text has no relaxation); `--clang`: clang's -S assembled
  by vpp runs as clang's own assembly (1432 tests); `vasm --target=arm64-apple-*`
  writes the same Mach-O object as vpp -c.  Left:
  `.cfi_*` is dropped: no compact unwind (`__LD,__compact_unwind`) in vcc's
  Mach-O, neither direct nor from text.
- [x] **Wrong output**: GOT relocs against local symbols kept the section
  (fixed: GOT/TLS relocations keep the symbol); integer and absolute-symbol
  branch targets are the pc-relative offset, not `*ABS*` relocations; `adr`,
  `adrp` and `ldr` literal take constants.  *Validated:* `vas-obj`
  `intbranch.s`, `absb.s`.
- [x] **Diagnostics**: (Done: errors inside a macro or `.rept`/`.irp` at
  `<instantiation>:N` with llvm-mc's "while in macro instantiation" notes: `err-macro.s`; vasm goes on after a statement's error as llvm-mc (at most 20), and
  still reports an unterminated `.if`: `err-many.s`; the statement's own line, `# N "file"` markers, end-of-input
  errors at the line after the last.)  *Validate:* `bench/vas-obj.py` error
  files (`err-*.s`: clang's file:line of every error and note).

### Linker (`ld/ld.c`) and binutils
- [ ] **ELF input parsing unchecked** [P]: (done: `obj_valid` in `ld/ld.c`
  checks every header, section, string table, symbol (`st_name`,
  `st_shndx`), relocation (symbol index, `r_offset` + field width) once, as
  lld's messages; `e_shnum == 0` takes the count from `sh[0]`; regular
  archive members (at 4 mod 8) are copied to aligned memory; archive headers,
  sizes and long-name offsets checked.  `bench/vlink-bad.py`: 29 corruptions
  under ASan+UBSan, each a clean error; vobjcopy/vnm's `elf_open` checks
  headers, sections, string and symbol tables (`elf_valid`), the same inputs
  without a crash.  Left: SHN_XINDEX symbols (rejected), a `fuzz-elf` target,
  a 70k-section object.)
- [ ] **Crashes on ordinary inputs** [P]: no script.  (Fixed: a script
  without `.symtab`/`.strtab`/`.shstrtab` places them as orphans, as lld;
  `/DISCARD/` of a merge section (`.comment`): `isec_osec`.  Both found by
  `VCC_LD=build/src/vlink bench/vcc-ctests.py --no-cache`, which links every
  test with vlink: 1750/1750; with `VCC_LDCMP=1` each test is linked by both
  and vlink's loaded image (objcopy -O binary) must equal lld's: 1750/1750.)
  *Validate:* T8 cases `no-script`, `discard-comment`.
- [ ] **Options**: (done: `-e`/`--entry` over `ENTRY()`, else `_start`,
  lld's warning when absent; `--orphan-handling=place|warn|error`; `-m X` /
  `-mX` checked against lld's AArch64 emulations ("unknown emulation");
  `-zX` as `-z X` (`-znotext` was swallowed); `--version`/`-v` with "LLD
  23.1.4" for `scripts/ld-version.sh`; `-static`/`-Bstatic`/`-Bdynamic`,
  `-no-pie`; `-L DIR`/`-lNAME`/`-l:FILE` (archives only, "unable to find
  library"); `-pie` and `--dynamic-linker` a clear error until dynamic
  linking.  `bench/vlink-opts.py`: 18/18 against ld.lld, image identical.)
  `--gc-sections` as lld's MarkLive (roots: entry, KEEP, SHF_GNU_RETAIN,
  non-alloc, init/fini arrays, notes outside groups, .ctors/.init/...,
  exported symbols under -shared; relocations, groups and SHF_LINK_ORDER
  propagate), `--print-gc-sections` on stdout as lld: 6 cases.
  `__start_X`/`__stop_X` for C-identifier output sections (protected,
  global; not a gc root: lld's default -z start-stop-gc); orphans
  stable-sorted by rank before placement as lld's sortSections: 2 cases.
  `.eh_frame` as lld's EhFrameSection: records split, CIEs merged by
  contents and relocations, FDEs of dead/discarded sections dropped, CIEs
  without FDEs dropped, a zero terminator, word aligned; under
  `--gc-sections` CIE relocations keep their targets, FDE ones only outside
  code and groups (MarkLive::scanEhFrameSection): 5 cases, and
  `VCC_LDCMP=1 VCC_CFLAGS=-fasynchronous-unwind-tables vcc-ctests.py`
  1756/1756 identical.  Orphans placed by lld's rank bits and
  getRankProximity, then back past sections without inputs (findOrphanPos).
  `--eh-frame-hdr` as lld's EhFrameHeader (sorted table, duplicate
  locations dropped) and PT_GNU_EH_FRAME: 4 cases.
  Left: `--emit-relocs`, `-z now` (no effect without dynamic linking).
  *Validate:* `bench/vlink-opts.py` per option; unknown options are errors.
- [ ] **Relocations**: (done: GOT relaxation adrp+ldr -> adrp+add / nop+adr
  as lld; orphans placed by lld's coarse rank (sections without inputs do not count; nothing with inputs after: at the very end, as lld); `.got` for
  `ADR_GOT_PAGE`/`LD64_GOT_LO12_NC` against globals, 0 for undefined weak,
  RELATIVE under -pie/-shared; `test/vcc/got_pic.c`, `pragma_weak.c`; locals
  after the globals by file as lld's postScanRelocations (`vlink-opts.py`); not:
  GLOB_DAT for preemptible symbols (dynamic linking)),
  TLS in shared objects (executables done: LE, IE and TLSDESC relaxed to LE as
  lld, `PT_TLS`, `.tbss` without address space; text byte-identical to lld
  for `test/vcc/tls_{static,ie,desc}.c`; `PT_GNU_RELRO` as lld: isRelroSection,
  one contiguous run, p_align 1, a new PT_LOAD at relroEnd, `-z norelro`;
  `VCC_LDCMP=1` also compares program headers: 1756/1756), MOVW_*, CONDBR19, TSTBR14,
  PREL32 to undefined (`ld/ld.c:2560-2570`).
  *Validate:* T8 (each against lld, run under qemu where runnable).
- [x] **`vpp -c x.s`** (done: `.s` and `-x assembler` go to the assembler
  without the preprocessor, as clang; an explicit `-x` takes any file name;
  objects identical to clang -c; `vlink-opts.py` builds its inputs so.)
- [ ] **Archives**: (done: lazy members extracted as lld: a non-weak
  undefined reference extracts the member defining it, depth first, in lld's
  file order; an archive's member is extracted when its definition meets an
  earlier undefined reference; weak references do not extract; order-free, so
  `--start-group` needs nothing; regular and thin archives.  A sequential pass
  only when a non-whole archive is linked.  `bench/vlink-opts.py`: 11 lazy
  cases, images identical to lld.  COMMON as lld: strong beats common beats
  weak; largest size and alignment, first file with that size; one NOBITS
  `COMMON` section each after all files' sections, in lld's
  replaceCommonSymbols order; 7 cases in `vlink-opts.py`.  COMDAT groups as lld: the first group of a
  signature in file order is kept, later members dropped; their definitions
  still resolve, and a winner in a dropped member is demoted with lld's
  "relocation refers to a symbol in a discarded section"; non-COMDAT groups
  kept; 5 cases.  Under `-r` as lld: a kept group is a `.group` section of
  its own (signature, members renumbered, a member's `.rela` too), members
  are output sections of their own with SHF_GROUP, a dropped COMDAT copy
  goes and its definitions become undefined: 3 `-r` cases in
  `vlink-opts.py`, groups and the final link of the output as lld -r's.)
  *Validate:* T8 static link of a musl `libc.a` hello world (run under
  qemu-user); a C++ template object pair.
- [ ] **Dynamic linking**: `.dynamic`, `.dynsym`/`.dynstr`, `.gnu.hash`, PLT,
  `DT_NEEDED`, versioning, RELR, `PT_INTERP`, `PT_GNU_RELRO`,
  `.eh_frame_hdr`, reading `.so` inputs (`ld/ld.c:1678`).
  *Validate:* T8 (shared lib + PIE executable against glibc's or musl's
  `.so` in a sysroot; run under qemu-user; `llvm-readelf --dynamic` diff vs
  lld); kernel vDSO linked by vlink and boot test.
- [ ] **Linker script**: MEMORY / `=fill` (`ld/ld.c:1406`), PHDRS (`ld/ld.c:1411`),
  file patterns (`ld/ld.c:1261`), EXCLUDE_FILE (`ld/ld.c:1277`), mixed SORT
  (`ld/ld.c:2221`), INSERT/INCLUDE/VERSION (`ld/ld.c:1515`), headers-not-in-segment
  (`ld/ld.c:3694`).
  *Validate:* T8 per construct vs lld.
- [ ] **`--gc-sections`, `--icf`, `--build-id` modes (`ld/ld.c:4026`), erratum
  843419, SHT_REL (`ld/ld.c:1735`)**.
  *Validate:* T8.
- [ ] **Diagnostics**: (done: undefined symbols as lld: every reference
  with its source file and enclosing function (else section+offset), the
  first 3 then "referenced N more times", archive members, "did you mean"
  for the first two, a blank line after a multi-line diagnostic;
  `bench/vlink-opts.py` compares the whole stderr.)  `-r` output differs from lld.
  *Validate:* T4-style expected-stderr for vlink; T8 `-r` byte diff.
- [x] **var**: (done: regular GNU archives created, updated (`r` by
  basename, none with `P`; `q`, `d`, `s`, `m`) and read (`t`, `p`, `x`),
  byte-identical to llvm-ar; its BSD format on macOS for a new archive whose
  first member is not ELF, kept on update; `/SYM64/` and `__.SYMDEF_64` past
  4 GB (`SYM64_THRESHOLD` as llvm-ar's, for tests); mode 644 for every
  member as llvm-ar's deterministic mode.  vlink reads the members' own
  symbols, so both symbol tables are skipped alike.)  *Validate:*
  `bench/var-cmp.py`: 416 kernel archives, 12 t/p, 111 edge sequences (37
  of them again with `SYM64_THRESHOLD=0`).
- [x] **vobjcopy / vnm**: (done: `-g`/`--strip-debug`, `--only-keep-debug`
  (relocatable objects), `--redefine-sym`, `--add-section`,
  `--add-gnu-debuglink`, `--set-section-flags`, `-O`/`-I elf64-littleaarch64`,
  `-I binary`, `-B`; `.llvm_addrsig`'s link dropped as llvm-objcopy; nm
  `-u`/`-U`/`-g`, `-D` with symbol versions, archives (regular and thin),
  long forms, unknown options are errors, any number of files.)
  *Validate:* `bench/vobjcopy-cmp.sh` extended with these flags vs
  llvm-objcopy/llvm-nm; unknown options must be errors.
- [ ] **macho.c**: DWARF dropped (`platform/darwin/macho.c:95`), `.eh_frame` to
  `__TEXT,__const`, no compact unwind, no `MH_SUBSECTIONS_VIA_SYMBOLS`,
  `LC_BUILD_VERSION` fixed at 11.0 (`platform/darwin/macho.c:428`).
  Without the flag ld64 cannot replace a weak definition of vcc's by a
  strong one ("duplicate symbol").
  *Validate:* T3 (`ld64 -dead_strip` removes an unused function;
  `dwarfdump` / lldb line lookup; `-mmacosx-version-min=14.0` in
  `otool -l`); `bench/vcc-abi.py --host pragma_weak` (its xfail goes).

### Driver (`driver/ppdriver.c`, `server/vccd.c`, `server/capabilities.c`)
- [ ] **vccd compiles only `-c` of one `.c`/`.S` for aarch64 Linux; the rest
  goes to clang** (done: the input anywhere on the line, `-o` before `-c`,
  `-fPIC`, `-S` (without `-o`: the basename with `.s`, as clang); vccd-test
  `compile-shapes-not-forwarded`.  Left: Darwin,
  other targets).
  *Validate:* T11 (`-c a.c -o a.o`, flags after the input, `-o` before
  `-c`) all handled by vcc (a counter in vccd's stats, or `VCC_NO_FORWARD=1`
  that turns forwarding into an error).
- [ ] **No linking, no crt/sysroot discovery** (`server/capabilities.c:174`).
  *Validate:* T11 `vcc hello.c -o hello` on aarch64 Linux (qemu-user) and
  macOS; then an autoconf project (e.g. GNU hello) and a CMake project
  configure and build with `CC=vcc`, tests pass.
- [ ] **Unknown flags silently ignored** [P] (`driver/ppdriver.c:435-533`): `-g`,
  `-fsanitize=*`, `-m32`, `-march=znver4`, `-x c++`, `.cpp` inputs, `@rsp`,
  several inputs.  (Done: input detection by suffix, `driver_input_is_source`.)
  Every option is either implemented, explicitly ignored with a documented
  reason, or an error (`-Wunused-command-line-argument` like clang).
  *Validate:* T11 table generated from clang's option list
  (`clang --help-hidden`): each must be in an explicit table in
  driver/ppdriver.c; a test fails when an option has no entry.
- [ ] **Missing**: (done: `--target=T -dumpmachine` in vccd, vccd-test.)
  `-print-*` (`-print-file-name` echoes, `server/vccd.c:665`), `-v`, `-###`, `-save-temps`, `-shared`, `-l`/`-L`, `-Wl,`.
  *Validate:* T11 rows.
- [ ] **capabilities.c probe answers can contradict real compiles** (`server/vccd.c:291`,
  `server/capabilities.c:199-208`), unmatched sources forwarded to clang (`server/vccd.c:395`),
  every stdin link probe false (`server/vccd.c:353-356`), `.tmp_` heuristic
  (`server/vccd.c:252`), `--version` claims clang (`server/capabilities.c:163`).
  Make the table a cache of vcc's own answers (generated by running vcc,
  not clang), key it on target+flags, drop the filename heuristic, report
  an honest version string that CMake/Meson can identify (and a
  clang-compatible `-dM` set).
  *Validate:* `bench/caps-check.py`: for every table entry, run the probe
  for real through vcc with forwarding disabled and require the same
  answer; T11 for `.tmp_` paths compiled for real.

### rsh
- [ ] (Done: `trap` with EXIT/0 or no handler goes to the real shell,
  signal traps stay a no-op; rm/mkdir messages from strerror; fixdep only as
  a whole path component and an existing executable.)  **fixdep path match by suffix** (`utils/shell.c:414`): require the
  configured kernel tree.  **Wrong errno text** (`utils/shell.c:552,568,577`).
  *Validate:* `bench/vsh-test.py` rows against dash (trap, other fixdep
  paths, ENOENT/EACCES/ENOTDIR messages).

## Tier 4: quality

- [ ] **IPA constant propagation.** A static function's parameter that every
  call passes the same constant is that constant in the body (clang's IPSCCP,
  gcc's IPA-CP), noinline functions included: `__builtin_constant_p` of it is 1
  and branches on it fold.  VIR inlines once-called statics (walker,
  `fn_once`); calls it keeps see nothing of their arguments.
  *Validate:* `bench/vcc-ctests.py --suite inline`: `gcc.dg-ipa-pr92497-1`
  turns from xfail to XPASS (drop it from `INLINE_XFAIL`).

- [ ] **Optimizer shared across x86, arm64 and wasm**. Implementation
  owner: `PLAN-fas2.md`, **Shared optimizer without SSA**, OPT0–OPT8.
  arm64 already has register locals, copy/dead-result cleanup, stack-cell
  forwarding and extension facts; wasm already has scalar locals. Extract
  and reuse those algorithms rather than treating x86's gaps as missing
  from every backend. Share typed arithmetic, effect/reachability facts,
  local-candidate selection, inlining and switch policy in `codegen/opt/`;
  record algorithms in `codegen/rec/` for register targets. Target instructions,
  register assignment, ABI and structured wasm lowering stay with targets.
  The first rule must be consumed by all three backends, including wasm32
  and wasm64 for supported types. For `-O0`/`-O1`: no new IR/SSA or unbounded pass loops (`-O2` is owned by
  `PLAN-vir.md`).
  First delivered slice (2026-11-11): unsigned power-of-two div/rem in
  `codegen/opt/divpow2.c`, all three consumers, codegen/runtime checks at O0–O3.
  Integer identities now live in separate `codegen/opt/identity.c`, shared by
  all targets; keep future algorithms in named files, with small walker
  adapters. Both build modes pass 32 target/level/rule checks.
  Next slice: shared `codegen/rec/fallthrough.c` on arm64/x86; wasm
  terminal while-backedge cleanup in separate reachability unit. OPT2
  now also removes unreachable suffixes and has x86 effect metadata.
  Shared bounded block/liveness contracts now live in `codegen/flow/`.
  Wasm's separate typed-flow consumer removes overwritten private exact
  4/8-byte frame stores through structured joins; unique opening tokens
  preserve branch identity and epoch-scoped real fences preserve calls
  after empty arms. Loops, dispatchers/goto, asm, SJLJ and dynamic frames
  remain excluded. At `1d8e29a`, 114 focused/existing configurations and
  isolated epoch-coalescing mutations pass, plus the full own SDK suites:
  wasm32 178 PASS/36 XFAIL/16 skips, wasm64 182 PASS/37 XFAIL/11 skips.
  New fast harness: `bench/vcc-wasm-flow.py`; full results and limitations
  in PLAN-fas2/PLAN-wasm. Integrated `1458a90` against exact flow parent
  `f7327bd` again passes 114 checks and 66 byte-identical O0/noopt pairs.
  Matched v3 whole-process Callgrind controls cost +0.314–0.552%; fixed
  wasm32/64 mixes +0.106/+0.095%; concentrated positive diamonds cost
  +4.494–4.683%, reducing 800/1000 stores to 200 and relocatable object
  size 2.886–3.325%. Corpus exclusions, raw profiles and limits are in
  the plans; active analysis exceeds 1% on the positive probes. The full
  performance gate and remote CI remain pending; this does not close
  general DSE, O2 or G5/G7. Signed power-of-two
  recipes are in separate sdivpow2.c files; wasm chooses compact div/rem
  except cheaper -1/MIN cases. OPT3 register locals are next; builtin
  setjmp now preserves all potential promoted callee-save registers,
  including inherited R12; final alignment is reconstructed on resume.
  Shared locals.c eligibility/selection now feeds all targets and x86
  promotes profitable scalar locals in four callee-save homes. Direct
  ABI-to-home binding is now direct for selected register/stack parameters;
  `codegen/rec/copies.c` now shares arm64's forward-copy search with the x86
  adapter in `arch/x64/codegen/copies.c`: bounded transactional rewrites of
  full-width GPR temporaries within a block, with target effect/operand
  contracts. Native 32-bit/XMM copy cleanup, shared extension facts and
  improved profitability remain pending. Unity/nonunity builds, 32 focused
  target/level runtime combinations and normal/sanitized record contracts
  pass; prior arm64 debug/unwind and wasm output snapshots are unchanged.
  Short paired compiler CPU samples are noisy (even unchanged wasm control
  varies 17.5%); the unchanged sum program's runtime difference is noise.
  OPT4 documents raw before/after numbers; the 1% performance gate is open.
  Wasm checks use our assembler/linker and plain Node; full replacement
  of Emscripten's test/explorer runtime is tracked in `PLAN-wasm.md` W9.
  *Validate:* common C semantics plus target-specific code assertions;
  extraction commits preserve output byte for byte, activation commits
  pass suites/ABI/stress and relevant debug/kernel gates. Measure compiler
  instructions/cycles and generated-code runtime/size separately; old
  self-build/runtime ratios are historical, not current x86/wasm evidence.
- [ ] **Error recovery**: parse errors recover per block, struct body, for
  header and TU, missing closers as clang (PLAN-diag.md D2.2, D2.3: first
  error at clang's place 95.3 %); left: parameter lists: `f(a; )` (clang's
  "unexpected ';' before ')'"), broken attributes (`__attribute__(( (x))`),
  clang's "expected expression" for `4(` (vcc: "called object type 'int' is
  not a function"), "extraneous ')' before ';'" after a builtin's name, the
  remaining `fatal()` sites one by one; mutants with more follow-on errors
  than clang 4.8 % (goal 5 % reached).
  *Validate:* `test/diag/` cases with several independent errors, exact
  clang output (`vcc-diag.py`).
- [x] **Warnings as errors not controllable**: `-Wno-error=`/`-Wno-` of
  `int-conversion`, `incompatible-pointer-types` (its subgroup
  `incompatible-function-pointer-types` follows) and the function pointer
  flag; `-Werror=` of one again is clang's plain error (no `-Werror,`:
  `diag_warning_error_named`).  Done: diag `conv_flags*.c` (`diag-flags:` in
  vcc-diag.py), 12 flag combinations identical to clang.
- [x] **-Winteger-overflow**: signed `+ - *` and unary `-` of constants
  whose exact value does not fit (innermost only, `AST_NODE_FLAG_OVERFLOW_REPORTED`), value with
  clang's digit separators.  Done: diag `int_overflow.c`; not modelled:
  overflow inside casts/conditionals reached through other operators.
- [x] **-Wunused-variable**, `-Wunused-const-variable`: block-scope
  variables never named, at scope close; main-file statics at end of TU;
  none after an error, cleanup/unused/used exempt.  Done: diag
  `unused_var.c`, 90 warnings over the 2032 test-suite files identical to
  clang.  (A static redeclared and then defined: at the definition, `defloc`.)  Not modelled: bodies of unreferenced
  lazy header functions are never parsed, so they are not checked.
- [x] **-Wconstant-conversion**: an integer constant converted to a
  narrower integer type (init, assignment, argument, return; each `?:` arm,
  the taken one when the condition is constant; a comma's right operand).
  Narrowing waits for the end of the function as clang's
  DiagRuntimeBehavior; same-width sign changes skip literals spelled with a
  leading 0 (`AST_NODE_NUMBER` aux) and chars in braces.  Done: diag `const_conv.c`,
  kernel/PostgreSQL/test suites 0 differences.  Not modelled: clang drops
  the deferred ones in unreachable code (vcc: only under a constant-false
  condition, `c->dead`); a member
  initializer converted to a narrower type (`{ 300 }` for a char member).
- [x] **-Wliteral-conversion**: a floating literal (or its negation)
  converted to an integer type: fractional ("changes value from 1.5 to 1",
  APFloat::toString's spelling) or out of range.  Done: diag
  `literal_conv.c`; test suite 183 of 186 identical.  Not modelled: in a
  macro body the location and the macro note (no spelling to look up).
- [x] **-Wbitfield-constant-conversion** (and the single-bit variant): a
  constant stored in a bit-field, by assignment or initializer, as clang's
  AnalyzeBitFieldAssignment.  Done: diag `bitfield_const.c`, 93 warnings
  over the test suite identical.  Not modelled: the macro note finds the
  operator by spelling, so it can show an earlier `=` of the same body.
- [x] **-Wunused-value**: clang's isUnusedResultAWarning on expression
  statements, a statement expression's statements but the last, a `for`
  init, a comma's left operand (a comparison there: -Wunused-comparison);
  deferred to the end of the function with the conversion warnings
  (`rt_defer`).  Done: diag `unused_value.c`, `unused_value_comma.c`;
  kernel, PostgreSQL, test suite 0 differences.  Not modelled: the macro
  note for a macro argument (its tokens keep their file location); calls
  of pure/const functions ("ignoring return value of function declared
  with const attribute").
- [x] **pp -Wextra-tokens**: after #ifdef/#ifndef/#elifdef/#elifndef's
  name, #else, #endif, #undef's name, #include's header name and #line's
  file name, with clang's "//" fix-it; conditionals at the active level
  also when a skip lands on them.  With it: a #line file name now reaches
  the diagnostics (`diag_location_presumed` replaced it by the file's path).  Done:
  diag `extra_tokens.c`; parity cmp/lines green.  Not modelled: a
  macro-expanded #include name.
- [x] **pp `#pragma GCC warning`/`error`** ("msg" or ("msg"), else
  "pragma warning requires parenthesized string") and
  **-Wexpansion-to-defined** (a `defined` from an object-like macro in
  #if; function-like: pedantic in clang, not given).  A macro note after a
  diagnostic without ranges is a bare caret, as clang.  Done: diag
  `pragma_warning.c`.  Not modelled: from `_Pragma` (clang's
  "<scratch space>" note).
- [x] **pp -Winvalid-pp-token**: an unterminated ' or " in a #define
  body (MF_UNKNOWN, so a cached site does not rescan) or in the text (given
  by the front end with its error: an unknown token always ends in one, so
  no per-token check).  Done: diag `invalid_pp_token.c`.  Not modelled: in
  -E output; clang's recovery after it ("expected ';'").
- [x] **-Wunused-function**, `-Wunneeded-internal-declaration`: static
  functions of the main file never used, at the definition (`defloc`);
  named only unevaluated (sizeof): "not needed and will not be emitted";
  cleanup() and alias() targets count as used.  Done: diag
  `unused_function.c`; kernel 879/879.  Not modelled: clang also warns a
  non-inline static function of a header (vcc cannot see the references
  from unparsed lazy bodies) and a function only calling itself.
- [x] **-Wunused-but-set-variable**: clang's RefsMinusAssignments
  (`sym.nref`: references, minus discarded full expressions and comma left
  operands that only assign, ++ or -- it; volatile compound ones count),
  given with the unused variables at scope close.  Done: diag
  `unused_but_set.c`; the 31 test-suite warnings identical.
- [x] **Missing warnings**:
  (`-Wunknown-attributes` and
  `-Wdeprecated-declarations` are done: PostgreSQL's warnings compare).
  *Validate:* one diag test per warning; `vcc-sema.py warn` stays 879/879
  (no false positives on the kernel).  (Done: a read after a goto or a case label that skips the assignment
  (the analysis also runs after a jump), a case reached by a fallthrough that assigned it
  ("whenever switch case is taken"): diag `uninit_jump.c`; up to 8 ifs breaking out of one `do … while (0)`, one warning each, with
  clang's multi-line range snippets: diag `sometimes_uninit_do2.c`; `-Wsometimes-uninitialized` of an if that breaks out of `do … while (0)` (no
  back edge: a block), and with `-Wsometimes-uninitialized` alone: diag
  `sometimes_uninit_do.c`, `sometimes_uninit_only.c`; `-Wmultichar` in C and `#if`, "character constant too long",
  multi-character wide/Unicode literals as errors: diag `multichar.c`; `-Wself-assign` (not through macros) and `-Wunused-local-typedef`: diag `self_assign.c`, PostgreSQL identical; `-Wreturn-stack-address` of a compound literal (its initializer printed
  from the source as clang's printer spaces it): diag `return_complit.c`; `-Wuninitialized` of struct/union variables (whole-value uses; a member access
  initializes, an empty record is not tracked): diag `uninit_record.c`; `-Wsometimes-uninitialized` of a while/for condition (a loop that never loops
  back): diag `sometimes_uninit_loop.c`; `-Wsometimes-uninitialized` of `&&`, `||`, `?:` and switch labels (a warning per
  label edge, clang's CFG order): diag `sometimes_uninit2.c`; `-Wsometimes-uninitialized` of an `if` (the use post-dominates it, as clang's
  branch search; the notes with clang's ranges): diag `sometimes_uninit.c`; `-Wuninitialized`
  (definite uses, a dataflow over the statement tree in flow.c, clang's report order
  by CFG block, self-initialization, the fix-it): diag `uninit.c`, `uninit_kinds.c`,
  the test suite gives no warning clang does not; the
  diagnostic printer leaves out the snippet for a repeated location as clang.)
  (Done: `-Wreturn-type` (fall off the end, "in all control paths") and
  `-Winvalid-noreturn` (also a return in a noreturn function), "no case
  matching constant switch condition": `frontend/c/flow.c`, reachability as clang's
  CFG with constant conditions (also && / || / const variables, the kernel's
  scoped_guard), noreturn calls and goto; diag `return_type*.c`, kernel warn
  879/879, PostgreSQL no differences.)
  (Done: `-Wexcess-initializers`: excess array/struct/union/vector/scalar
  elements once per list, strings too long for their char array; an excess
  braced element is skipped whole (it stopped at its first token): diag
  `excess_init.c`.)
  (Done: `-Wreturn-stack-address` for locals and parameters through &, array
  decay, members, indexing, casts and ?: (diag `return_stack.c`).)
- [x] **`u8'a'` in C23**: the lexer still gives `u8` and `'a'`; the front
  end and `#if` join them under `-std=c23`/`gnu2x` when adjacent.  Done:
  diag `u8_char.c`, `u8_char_if.c` (and `pp_binop.c`: #if's 'not a valid
  binary operator' at the token with the operand underlined, as clang).
- [x] **Invalid code accepted**: duplicate enumerator / parameter,
  `&register_var`, `_Alignas(3)`, `typeof` on a bitfield, `*(void *)p` as a
  value (and `-Wvoid-ptr-dereference`), `return v();` of void from an int
  function, and the static initializer constness check in the front end
  (`static_init_ok`, also under `-fsyntax-only`).  Done: diag tests
  identical to clang (74/74); sema 878/879, warn 879/879, ctests, pg green.
- [x] **Runtime-behaviour warnings in a global initializer**: a static
  initializer's -Wdivision-by-zero waits and follows the constness error,
  only for a non-constant initializer (clang's DiagRuntimeBehavior); none
  under sizeof/typeof.  Done: diag `mod_zero_const.c`, `mod_zero_const2.c`.
- [x] **`"abc"[1]` as a static initializer**: an element of a string
  literal folds (`eval`); `{ "q"[0] }` in an int array parsed as a string
  initializer.  Done: `test/vcc/static_strelem.c`.
- [x] **Typedef names in -Wformat**: variables, parameters and members keep
  the typedef they were declared with (`sym.tdef`, `field.tdef`): messages
  print `'size_t' (aka 'unsigned long')`, fix-its use z/j/t and `%hhu` for a
  char typedef as clang's namedTypeToLengthModifier.  Done: diag
  `format_typedef.c`, the random files identical on both targets.
- [x] **-Wformat not modelled**: (Done: typedef names through explicit
  casts, calls, indexing, `*`, arithmetic, `?:`, comma and assignment as clang
  keeps the sugar (a `?:` range from its condition): `format_typedef_expr.c`; the printf family as
  library builtins without an attribute (declared with the builtin's type, not
  under -ffreestanding/-fno-builtin), `%b`/`%B`, `%q`, a const array or const
  pointer initialized with a literal ("format string is defined here"): diag
  `format_builtin.c`, `format_var.c`, `format_freestanding.c`; `format_arg`
  (gettext), MS `I`/`I32`/`I64` and `w`, `*n$`: `format_more2.c`; Darwin
  `%D/%O/%U`: `format_darwin_dou.c`.)
  *Validate:* diag tests against clang.
- [x] **Vector types in messages**: (done: the typedef name and
  ` (vector of 4 'int' values)` after the quoted type (vcc: the first typedef
  of that vector type), diag `vector_typedef_name.c`; with it
  -Wincompatible-pointer-types-discards-qualifiers (also "in nested pointer
  types"), -Wpointer-sign, nested signedness as incompatible, the
  "; dereference with *" hint and fix-it (simple operands), `int *const`
  spelled as clang, a decayed array named `int[2]`, and "not a compile-time
  constant" after a conversion error: diag `ptr_discards_quals.c`,
  `deref_hint.c`, `ptr_const_spelling.c`; `ext_vector_type` splat (AST_CAST_SPLAT),
  the scalar-to-vector truncation error, `.xyzw`/`.rgba`/`.sN`/`.hi`/`.lo`/
  `.even`/`.odd` swizzles: `test/vcc/ext_swizzle.c`, diag
  `ext_vector_splat.c`, `vector_scalar_trunc.c`, `ext_swizzle_range.c`.)
  Not done: assignment to a multi-component swizzle; a `long` constant out
  of an unsigned lane's range is an error in vcc, a -Wconstant-conversion
  warning in clang.
  *Validate:* diag test with vector_size and ext_vector_type.
- [ ] **Diagnostic details**: columns on `##` and invalid directives,
  `#line` file names ignored, nested macro notes (one level today), the
  "expanded from macro" note only for undeclared identifiers.  (Done: ranges
  keep a parenthesized operand's parentheses as clang's ParenExpr, `rng_np`
  where clang strips them (-Wconstant-conversion, -Wunused-value, the
  uninitialized notes); `c->paren` holds them as distances, so the decl
  cache and replay copy them without fix-ups: diag `paren_ranges.c`.)
  *Validate:* diag tests.
- [x] **Diagnostics found missing (2026-11-10)**: done.  -Wshift-count-*,
  -Wdivision-by-zero and -Warray-bounds (with its note) at the end of the
  function (DiagRuntimeBehavior, rt_defer); -Wunused-variable kept after a
  DefaultError conversion error (clang drops it only after a hard error):
  `runtime_order.c`.  Self-comparison: `self_compare.c`; "; take the address
  with &", parenthesized-operand hints, unnamed parameter notes:
  `int_conv_hints.c`, `unnamed_param_note.c`; -Wconditional-type-mismatch,
  "*(" ")" hints, `p == 0.5`: `cond_mismatch_etc.c`.
- [ ] **Clang warns, vcc errors** (`bench/vcc-mutate.py show spurious`):
  `-Wreturn-mismatch` is not in the `return-type` group.
  *Validate:* `vcc-mutate.py` spurious count; diag tests.
- [ ] **`-Warray-bounds` on a trailing member array**: `p->a[5]` with `a` the last
  member (`int a[3];`) warns; clang does not (`-fstrict-flex-arrays` default: a trailing
  array may be a flexible one).  *Validate:* diag test, both orders of members.
- [ ] **Designated initializers found by PLAN-diag D1.0**: a designator on a
  scalar (`int a = { .x = 1 };`) fails with "expected expression" instead of
  clang's "initialization of non-aggregate type 'int' with a designated
  initializer list"; after a designator error (`{ .a.b = 1 }` on an array)
  vcc stops at "expected '}'" and the column is the member, not the `.`.
  `__atomic_fetch_add` on a `double *` is rejected (clang accepts).
  `vector_size(3)` on `int` says "invalid vector size" (clang: not an
  integral multiple of component size); vector element type errors point at
  the declarator, clang at the attribute.
  *Validate:* diag tests (`test/diag/excess_init.c` style).
- [ ] **Warnings in lazy bodies**: an unused `static` function outside the main
  file is never parsed (`frontend/c/lazy.c` `lazy_ok`), so its warnings are missing:
  clang warns `-Wreturn-type` for `static inline int f(void) { }` in a
  non-system header, vcc only with `VCC_EAGER=1`.  A cheap scan of the kept
  tokens, or parsing the body when a warning group it could hit is on and the
  header is not a system one.  *Validate:* a diag test with the function in a
  header; `vbench.py gate` (fe, cc).
- [ ] **Invalid code accepted**: `void f(static int x)` (clang: invalid storage
  class specifier in function declarator).  From `bench/vcc-mutate.py` (PLAN-diag D0.2, `show missed`): `main` with wrong
  parameter types, an array of an incomplete element type, a call to a
  function with an incomplete return type (clang: "calling 'f' with incomplete
  return type"; a definition is an error now), a K&R definition whose
  parameter count conflicts with an earlier prototype (`void bar(void);` then
  `void bar(x) { }`: clang "conflicting types for 'bar'").
  *Validate:* T4 each, then promote to diag.
- [ ] **Macro notes by spelling**: macro notes are found by spelling in the
  bodies (PLAN-diag.md D5.2), so a name built by `##` (`CONCATENATE`) is not
  followed and a duplicated token may pick the wrong body occurrence (kernel
  `-pedantic`: 1 031 of 1 536 chains identical to clang's).
  *Validate:* diag tests, the kernel chain comparison.
- [ ] **Merged diagnostics: cases from the D8 branch**: the merge with main kept main's
  implementation where both had one (`frontend/c/flow.c`, `frontend/c/fmt.c`, return-stack-address,
  unused-value/-comparison); the branch's dropped files (`git show ce5d47b:src/frontend/c/`
  `uninit.c`, `format.c`, `unused.c`, `attrcheck.c`) handled these, now marked
  `diag-differs` in `test/diag/*_v2.c`, `portability.c`:
  - -Wsometimes-uninitialized: `if (c && (x = g())) return x; return x;`, an `if` whose
    then-branch only breaks or returns, `if (c) x = 1; else if (d) x = 2;`
    (`uninit_v2.c`), and the edge and the use drawn as numbered path steps;
  - -Wreturn-stack-address: two shapes missed, one extra (`return_stack_v2.c`);
  - -Wunused-value/-Wunused-comparison: four cases (`unused_value_v2.c`);
  - -Wformat for scanf and its family (fmt.c checks printf only).
  *Validate:* `bench/vcc-diag.py`: the `diag-differs` markers go.
- [ ] **Longer fuzz runs** and pp-diff over the whole fuzz corpus (PLAN-fas2
  "Status fuzzning").  *Validate:* runs recorded in the plan, findings in
  `regress/`.
- [ ] **PostgreSQL completeness**: TAP tests (`-Dtap_tests=enabled`) not
  run; links still by ld64.  *Validate:* `bench/pg/build.sh` with TAP,
  same pass set as clang.
- [ ] **One set of data structures in `lib/`** (restructure 2026-11-09).
  Merged so far: `vreserve` (4 copies), `vregion` (as + object writer),
  `vbuffer` (pp diagnostics + fixdep).  Still side by side in `lib/`: the
  buffers `vbuffer`/`vbuffer`/`vbuffer`/`vbuffer`/`vbuffer` (they differ in OOM
  behaviour: abort, sticky flag, bool) and `tbuf.h`; the arenas `vregion`
  (offsets) and `varena` (pointers); the hashes `vhash_string`/`vhash_string`.
  Still in their component, tied to its state: ppdriver's `buf` (first
  16 MB from a per-job vjob_region), cc's `vec` (inline storage, frozen into
  `c->lists`), ld's block arena (process-wide list, generations), as's
  `.req` hash, objcopy's StrTab and the object writer's symbol table.  One
  OOM policy for code inside vccd (fail the request, never abort) decides
  the buffer.  *Validate:* outputs byte-identical (objects, `-S`, `-E`),
  `bench/vbench.py gate`.
- [x] **The linker as a split module** (restructure 2026-11-09; done 2026-11-13:
  `ld-internal.h`, the link context, the fragments as `arch/*/ld/*.c`, each part
  compiles alone with `make VCC_UNITY=`; doc/quality/WORK-linker.md step 2).  `ld/ld.c`
  is one TU of ~4400 lines over process-wide globals (`objs`, `isecs`,
  `gsyms`, ... of anonymous `VVEC` types); its arm64 parts are fragments
  (`arch/a64/ld/*.inc`) it #includes.  Make it files by topic with an
  `ldimpl.h` (named vector types, VPRIV globals), as frontend/c/pp/emit, and the
  fragments real files.  *Validate:* `bench/vbench.py check --base` (the
  vmlinux link byte-identical), `bench/vobjcopy-cmp.sh`, gate.
- [ ] **Shared emitter semantics and incremental walker convergence**
  (revised 2026-11-11; `PLAN-fas2.md` OPT0–OPT8). Do not wait for x86 X4
  before sharing code. `walk.h` currently binds LO to x64 and `wkarg` is
  SysV-specific; it is not yet a target-neutral walker. Extract semantic
  helpers consumed by the three existing generators first. Extract arm64
  record algorithms with explicit width/effect adapters for x86; equal
  16-byte layouts do not imply equal field semantics. Retain wasm's stack
  values, locals, structured blocks and br_table while sharing the policy.
  Full walker consolidation can follow once real common interfaces emerge;
  remove duplicate rules when their shared replacement has all consumers.
  *Validate:* byte-identical output for extraction, common semantic tests
  for all consumers, unity/nonunity builds and measured compiler cost.
- [ ] **Target-OS code out of the generic files** (restructure 2026-11-09).
  `platform/` has whole files only (Mach-O writer, ELF writer); the
  target-Darwin branches (`DARWIN(c)` and `PP_DARWIN` tests, ~25) are still
  inline in `codegen/tu.c`, `codegen/type.c`, `codegen/data.c`, `arch/a64/codegen/`
  (call, value, func, builtin, lval, abi), `frontend/c/tu.c`, `frontend/c/layout.c`,
  `driver/ppdriver.c` and `server/vccd.c`.  Give them hooks in `platform/<os>/`
  once the x86-64 backend shows which differences are per OS and which per
  CPU (Apple arm64's ABI is both).  *Validate:* outputs byte-identical,
  `bench/vcc-abi.py --host`.
