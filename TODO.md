# vcc: TODO before a production compiler

From the production-readiness review of 2026-11-08 (six component reviews,
probes against clang 23).  Items are ordered by risk: tier 1 is silent wrong
code, tier 2 makes vccd safe to keep running, tier 3 is missing features,
tier 4 is quality.  Every item names where it lives and how it is validated;
an item is done when its test exists, fails before the fix and passes after,
and the existing gates (AGENTS.md "Build and test") stay green.

Notation: **[P]** confirmed by a probe during the review, **[R]** from reading.

File references: `emit.c:N`, `cc.c:N` and `pp.c:N` predate the split into
`emit/` (+ `arch/a64/emit/`), `cc/` and `pp/` (2026-11-08), and line numbers
in `as/as.c`, `obj/obj.c` and `ld/ld.c` predate the restructure of
2026-11-09 (AGENTS.md "Layout"), which moved parts of them to `arch/a64/`
and `platform/linux/` and shortened the files whose buffers moved to `lib/`
(`vccd/ar.c`, `vccd/rsh.c`, `vccd/fixdep.c`, `cc/layout.c`, `pp/diag.c`);
search for the function named in the item.
The test kinds referenced below are defined in the "Test infrastructure" section
first; most items only add a file to one of them.

## Test infrastructure (build these first)

Existing gates that every change must keep green: `vcc-diff.py cmp`
(pp parity), `vcc-sema.py run` + `layout` (878/879), `vcc-ctests.py`
(1708/1708), `vcc-diag.py` (63/63), `vcc-sema.py warn` (879/879),
`make -C ccs self-check`, `make -C ccs fuzz-regress`, kernel boot.

- [x] **T0 performance gate.** (done: `bench/vbench.py gate`.)  Every item below must leave the hot path
  alone: `bench/vbench.py` (kernel front end + emit, fixed corpus) before
  and after each change, budget +1 % per item, numbers in the commit
  message.  Features that cost time when used (`-g`, warnings that need
  dataflow, an optimizer) are measured with the flag on and off separately.
- [x] **T1 `vcc` suite: self-checking regressions under qemu.** (done: `bench/vcc-ctests.py --suite vcc` with xfail markers, 9 tests added, all xfail today.)
  `ccs/vcc/tests/vcc/*.c`, gcc-torture style (exit 0 or `abort()`), the
  suite in `bench/vcc-ctests.py` (`tests()`: `--suite vcc`).  Clang
  is still the oracle, so a test also documents clang's behaviour.  Supports
  `/* vcc-flags: ... */` and `/* vcc-threads */` (link the runtime's tiny
  thread shim, or run on the Darwin host, see T3).  One file per fixed bug.
- [ ] **T2 `abi` suite: cross-compiler calls.** (partial: `bench/vcc-abi.py`, Linux/qemu and `--host` Darwin halves, xfail markers; no generator yet.)  `ccs/vcc/tests/abi/NAME/{caller.c,callee.c}`;
  the runner builds the 4 combinations (clang/vcc × caller/callee) and runs
  each, on Linux (qemu, `tests/rt`) and on the Darwin host (native, system
  libc).  Covers argument passing, returns, varargs, layout of shared structs.
  A generator (`bench/abi-gen.py`, random signatures with a fixed seed, like
  gcc's `struct-layout-1`) gives breadth; hand-written cases pin known bugs.
- [ ] **T3 `host` suite: native Darwin runs.**  Same `vcc` suite files built with
  `vpp -c --target=arm64-apple-darwin`, linked with ld64 against libSystem,
  run natively.  Needed for threads, TLV, real libm, and Darwin ABI items.
  `make -C ccs host-tests`.
- [ ] **T4 `reject` suite: must-diagnose tests.**  `tests/diag/` already
  diffs stderr exactly with clang; add `tests/reject/*.c` where only "vcc
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
  Run also with `--bin-dir build/ccs-asan`.
- [ ] **T6 `obj` checks: object-level assertions.**  `bench/vcc-objcheck.py`
  with `tests/obj/*.c` carrying `// CHECK-RELOC: R_AARCH64_ADR_GOT_PAGE sym`,
  `// CHECK-SECTION: .eh_frame`, `// CHECK-NOT-SYM: x`, checked with
  `llvm-readelf -r -S -s` / `llvm-objdump -d`.  For PIC, TLS, CFI, DWARF,
  section groups, branch ranges — things that are about the object, not
  runtime output.
- [ ] **T7 `as` corpus: compiler-generated assembly.**  `tests/as/*.c` →
  `clang --target=aarch64-linux-gnu -O2 -S` (and `-g`, `-fPIC`, `-march=armv8.5-a+sve2`)
  → assemble with `vasm` and with clang, compare objects with
  `bench/vas-diff.py` (sections, symbols, relocations, bytes).  Plus
  per-instruction encoding tables generated from llvm-mc
  (`bench/vas-enc.py` extended: every mnemonic × operand form in a list,
  diff `llvm-objdump -d` text).
- [ ] **T8 `ld` suite: links against lld.**  `tests/ld/NAME/` with sources,
  a command line and an optional script; `bench/vld-cmp.py` links with
  `vlink` and `ld.lld`, compares `llvm-readelf -a` (normalised), and where
  runnable, runs both under qemu-user or qemu-system and compares output.
- [ ] **T9 more fuzz targets** (`ccs/fuzz/`): `fuzz-cc` (front end on pp
  output, `-fsyntax-only`), `fuzz-emit` (front end + emit to a buffer; with
  csmith/yarpgen programs as a differential oracle under qemu), `fuzz-as`
  (assembler text), `fuzz-elf` (ld `parse_obj`/`add_archive`, objcopy
  `elf_open`, nm on raw bytes), `fuzz-ar`.  Each with `regress/<target>/`
  replayed by `make -C ccs fuzz-regress`.
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
- [ ] **T13 real programs with their own test suites.**  Like PostgreSQL
  (42ecfb3, `meson test` as the oracle): build each with clang and with vcc
  and compare test outcomes, not just "green".  Candidates with strong
  self-checking tests: sqlite (`testfixture` + tcl tests, `speedtest1`), lua
  (official test suite), zlib/zstd/xz (round trips and checksums over a
  corpus), musl + libc-test (integer, string and FP edge cases), CPython
  (`make test`).  Darwin host first, Linux user space once PIC/PIE exists.
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
  `cc/` and `emit/` branches no suite reaches; each gets a targeted T1 test.
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
- [ ] **rt `%Lg` with clang's caller**: `printf("%Lg", f())` of 19.0000000000009L prints
  `1.9e+01` in the clang+rt reference (vcc's build prints `19`, the right answer).  Seen in tests/vcc/deep_mixed.c,
  which prints `(double)` with `%.17g` instead.
- [ ] **T18 runtime gaps hide torture tests.**  34 gcc/tcc tests are
  `ref-fail` (`vcc-ctests.py --show ref-fail`): ~15 only because
  `tests/rt` lacks `pow`, `tanh`, `__divsc3`, `__fixsfti`, `tmpnam`,
  `atomic_signal_fence`, or clang leaves `__builtin_exit`/`__builtin_puts`/
  `__builtin_mul_overflow_p` as calls.  Add them to the runtime (or a
  per-test shim) so the tests run.  *Validate:* `ref-fail` count drops,
  the newly run tests pass or become xfail with a TODO item.
- [ ] **T19 ASan inside our arenas.**  Poison the mmap'ed arenas behind
  `used` (`ASAN_POISON_MEMORY_REGION` under `__has_feature`), so
  over-reads there are reported.  *Validate:* an injected over-read in a
  test build is reported; ASan runs of the gates stay clean.

Order once Tier 1 is clear: T10 (finds most wrong code per hour), T2's
generator, T14 KUnit, T15 static sizes, T13, T16, T17 after PIC/PIE.

## Tier 1: silent wrong code

- [x] **Branch range: no relaxation for b.cond/cbz/tbz/adr** [P].  Done for
  functions without inline asm (`r_relax` in emit.c: b.!cond .+8; b label,
  iterated to a fixed point, only for functions over 1 MB); with inline asm,
  and for `adr` to a label beyond 1 MB, an error instead of wrapped code
  (relaxation there is still to do).  The emitter has no tbz.  Left: the
  assembler's integer targets (`b.eq 0x100000000`), see the assembler section.
  `arch/a64/a64.h:129-140` mask the offset; `enc()` in `emit.c:1484`.  Out-of-range
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
  (emit/stmt.c).  `slot()` `emit.c:2224`.
  Realign: when a function has a local with alignment > 16, align sp
  (`and sp, x?, #-A`) after the frame setup and address such locals off a
  base register (x19 or sp), keeping x29 for the frame.
  *Validate:* T1 `align_local.c` (`_Alignas(32/64/128/4096)` locals, arrays
  and VLAs of over-aligned types, inside recursion and alongside alloca;
  check `(uintptr_t)&x % A == 0`).
- [x] **`_Atomic` dropped** [P].  Done: `_Atomic(T)` and the qualifier
  (also `T *_Atomic`) make a type of its own (`TF_ATOMIC`, `ty_atomic()` in
  cc/type.c) with clang's layout; lvalue conversion (`decay_slow`) yields
  T; emit loads with `ldar`, stores with `stlr`, and every compound
  assignment and `++`/`--` is a `casal` loop around the operator's usual
  code (`atomic_rmw`, emit/expr.c).  Tests: `atomic_layout.c`,
  `atomic_ops.c`.  Left, each a clean error today: 16-byte atomics
  (`_Atomic __int128`, `long double`; needs `caspal`), loads and stores of
  whole `_Atomic` structs/unions, `_Atomic` vectors.  Differs from clang on
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
- [x] **VLA pointer `++`/`--`/`+=`/`-=`/difference ignore runtime size** [P].
  Done (emit/expr.c: assign_op, incdec_op, pointer difference).  Found
  with it: `int a[6][n]` (constant outer bound) was laid out as a 0-byte
  fixed array that overwrote other locals; an array of VLAs is now a VLA
  (cc/declarator.c).  VLA sizes are evaluated once where the declaration
  is reached (parameters: on entry) and kept in a slot (emit/stmt.c
  vla_fix), so a later `n = ...` no longer changes `sizeof` or the step.
  Was: `ptr_add_vla` `emit.c:6051` only covers binary `+`/`-` and subscripting.
  *Validate:* T1 `vla_ptr.c` (`int (*q)[n]`: `q++`, `q--`, `q += 2`,
  `q -= 1`, `q3 - q`, `&q[1] - q`, 2-D VLA of VLA).
- [x] **VLA with initializer accepted; garbage at run time** [P].  Done:
  rejected as clang does, same location and range (cc/decl.c); `= {}`
  (accepted by clang in every mode) zeroes the array at run time
  (emit/stmt.c gen_vla_decl).  Tests: `tests/diag/vla_init.c`,
  `tests/vcc/vla_zero.c`.
  Was: reject
  as clang does ("variable-sized object may not be initialized"), except
  `= {}` in C23 which zeroes.
  *Validate:* T4 (`int a[n] = {1};` errors); `tests/diag/vla_init.c` once
  the message matches; T1 for the C23 `= {}` zeroing when C23 lands.
- [x] **Vector types passed in GPRs** [P].  Done: `hfa()` (emit/abi.c)
  takes short vectors (8/16 bytes) alone and as HVA members (1-4 of one
  size, never mixed with floats), so arguments, returns and va_arg use v
  registers; vectors under 8 bytes are returned in v0 as clang/LLVM does
  (lanes widened to 8 bytes; `vec_small`).  Tests: `tests/abi/vec_args`,
  `tests/abi/vec_small_ret` (bench/vcc-abi.py, Linux).  Left: the Darwin
  host half (T2/T3), variadic vectors on Darwin.
  Was: `is_recabi` `emit.c:98`.
  Short vectors (8/16 bytes) go in v registers per AAPCS64 (and HVA
  classification for structs of them, see tier 3).
  *Validate:* T2 `vec-args` (int32x4, uint8x8, `vector_size(16)` float/int,
  as arguments, returns, varargs, mixed with HFA) both targets.
- [x] **Darwin: narrow integer returns not extended** [P].  (done:
  `tests/abi/narrow_int`, `bench/vcc-abi.py --host`; arguments too.)  Apple's ABI
  requires the callee to sign/zero-extend `char`/`short`/`bool` returns to
  32 bits (and the caller for arguments).  Extend at return in emit.c when
  the target is Darwin.
  *Validate:* T2 `narrow-ret` on Darwin (`retc(0x1ff)` → -1, `bool`,
  `unsigned short`, through function pointers and tail position).
- [x] **Darwin: unnamed bitfields add alignment** [P].  (done:
  `bench/vcc-bflayout.py`, both targets, and `tests/abi/bitfield_layout`;
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
  decimal→binary128 converter; the tf soft-float in `tests/rt` can be the
  check), fold in binary128.
  *Validate:* T1 `ldouble_const.c` (`0.1L != 0.1`, `1e4000L` finite,
  `LDBL_MAX`, `LDBL_MIN`, denormals, hex float `0x1.0000000000000000000000000001p0L`,
  bit patterns compared via `memcpy` against clang's).
- [x] **`#pragma weak` ignored** [P].  `cc.c:892` handles only `pack`.
  Implement `#pragma weak sym` and `#pragma weak alias = target`.
  *Validate:* T6 `pragma_weak.c` (symbol binding WEAK, alias defined);
  T1 `pragma_weak_undef.c` (`&ext == 0` at run time, not folded to 1).
  (done: clang's rules in `cc/decl.c`, pending pragmas included, and its
  diagnostics; on Darwin the aliases are local, as clang's.  Also fixed: a
  weak symbol's address, by attribute too, was folded as nonzero in the
  front end and the emitter, and Darwin aliases lacked the `_` on their
  target.  Bindings by the linker in `tests/abi/pragma_weak` (T6 does not
  exist), run-time zero in T1 `pragma_weak.c`, warnings in
  `tests/diag/pragma_weak.c`.)
- [x] **`#pragma pack(push, ident, N)` ignores N; `pop, ident` pops one** [P].
  (done: `pragma_pack` in cc/token.c with clang's rules: labels, `pop, id`
  to the label (a missing one pops nothing), `pop, N`, `pack(0)` = default,
  invalid values and malformed pragmas ignored whole; tests
  `tests/vcc/pragma_pack.c`, `pragma_pack_pop.c`.  clang's
  -Wignored-pragmas warnings are not given.)
  `cc.c:919-927`.
  *Validate:* layout `_Static_assert` cases against clang (all forms:
  `push`, `push,N`, `push,id`, `push,id,N`, `pop`, `pop,id`, `pop,N`, `pack()`,
  `show` ignored).
- [x] **Designated-initializer overrides differ from clang** [P].
  (done, cc/init.c: an array element or range a later designator covers is
  dropped, not evaluated; a designator into a sub-object initialized by one
  value keeps the rest of it: a string becomes one element per character,
  a struct/array value one member access / element per part, a union value
  is read again through the member the designator picks.  Left, a clean
  error: the overridden value has side effects (clang evaluates it once).
  Only a run that starts below an earlier one's end scans for covered runs,
  so in-order lists stay linear (`init_large.c`, `vcc-timeout: 10`).
  Tests `tests/vcc/desig_override.c`, `desig_override2.c`.)
  `.s = "ab", .s[3] = 'z'` loses "ab"; `.i = iv, .i.y = 99` zeroes `i.x`;
  `[0] = side(), [0] = side()` evaluates both.
  *Validate:* T1 `desig_override.c`, static and automatic variants, with
  side-effect counters (clang's behaviour is the oracle).
- [x] **Header/snapshot cache keys incomplete** [R].  *Done:* `opt_sig` hashes every
  `cc_opts` field before `outfd` (cc.h says parse options go there) plus emit;
  `sig_of` adds wargs and error_limit; `bench/vcc-cachekey.py` 6/6 (the decl
  cache rows need bigger headers to reach a restore).  Was:  `opt_sig`
  `driver/ccsnap.c:356` misses `-std`, `-fgnu89-inline`, `-finstrument-functions`,
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
  (emit/asm.c); `tests/vcc/asm_uniq.c` with the new `vcc-threads-same`
  marker (`-S` equal with 1 and 8 threads; failed before) and `vcc-noref`
  (clang rejects `%=` on AArch64).)
  `atomic_fetch_add(&E->asmno)` `emit.c:8010`.  Number per function
  (function index × asm index within it).
  *Validate:* T6/`vcc-obj.py`: compile a TU with several `%=` asms in many
  functions with `VCC_THREADS=1` and `=8` ten times; objects byte-identical.
- [x] **LSE-only atomics** [R].  (done: without LSE (Linux default, armv8-a,
  -mcpu=cortex-a53 ..., `+nolse`) every casal/swpal/ld<op>al is an
  ldaxr/stlxr loop (emit/builtin.c at_cas/at_swp/at_ldop, also the
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
  attribute: msg", once per call site (emit/call.c); the old drop-and-go-on
  rule only with `-fvcc-kernel`, which `-D__KERNEL__` implies.  Test
  `tests/diag/attr_error.c` with the new `diag-mode: -c` marker.  Differs:
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
  accept (getpeereid / SO_PEERCRED).  The other-uid test needs sudo.  Was:  `vccd/vccd.c:924-927`,
  `vccd/shim.c:85-135`.  Socket in a per-user 0700 directory
  (`$XDG_RUNTIME_DIR` or `$TMPDIR/vccd-$UID/`), check the directory's owner
  and mode before connecting (shim) and before binding (server), and check
  the peer uid with `getpeereid`/`SO_PEERCRED` on every connection.
  *Validate:* T12 (mode 0700 dir, 0600 socket; connection from another uid
  refused; pre-created squatting socket/dir detected by the shim and refused).
- [x] **ld.lld spawned with the request's environment** [R].  *Done:* spawn drops
  `DYLD_*`, `LD_PRELOAD`, `LD_LIBRARY_PATH`, `LD_AUDIT` (blacklist, so tools keep
  the variables they read); vccd-test spawned-tool-env-clean.  Was: `vccd/vccd.c:543`.
  Whitelist (PATH and the few the tools read); drop `LD_*`/`DYLD_*`.
  *Validate:* T12 (`DYLD_INSERT_LIBRARIES=/nonexistent` in the request does
  not change the child; a test helper dumps its env).
- [x] **No protocol/build versioning** [R].  *Done:* magic "roo2", the header
  carries a build id (size+mtime of the vccd next to the shim); vccd answers
  VROOM_STATUS_MISMATCH and the shim says to restart it (vccd-test
  other-build-refused).  No automatic restart.  Was:  `protocol.h:12`.  Add a build
  id (hash of the binary or a build-time constant) to the handshake; on
  mismatch the shim restarts the server or falls back.
  *Validate:* T12 (shim from build A against vccd from build B → clear
  error or restart, never silent service).
- [ ] **Input-driven `abort()` / stack overflow kill the whole server** [P].  (recursion
  done: `lib/vstack.h` checks the stack pointer in cast_expr, the #if evaluator
  and argument pre-expansion; 128 KB left ends the TU (clang itself crashes
  on these inputs).  Overflow is a per-TU error now too: pp's
  token stack via fatal(), as/elf regions via `vjob_fail` (obj/obj.h, a longjmp
  the driver sets); vcc-stress 19/19, --vccd 20/20.  Audited the rest: input-driven
  ones are errors now (front-end heap: fatal; labels: efatal; pp shared region,
  identifier table: `vjob_escape` in lib/intern.c; ELF sections: vjob_fail); the
  remaining aborts are mmap failures and internal invariants.  No stress case
  yet for the front-end heap, labels or sections.)
  Aborts: `emit.c:1934,2036,2778`; `cc.c:44,1518,6219`; `as/as.c:107`;
  `obj/obj.c:33,40,234`; `pp.c:89,301,1721,2145,2531,2594`; `lib/intern.c:96,195`;
  `lex/lex.c:1318,1394`; `driver/ppdriver.c:65`.  Recursion without depth limit: cc.c
  expression/declarator parser, pp `#if` evaluator and argument
  pre-expansion.  Turn arena/limit failures into per-TU fatal errors
  (longjmp to the job, as `fatal_jb` already does in pp), add depth limits
  matching clang's (bracket depth 256 by default, `-fbracket-depth`), and
  check `vmreg` reservations in `mk`/`sym`/`tag`.
  *Validate:* T5 (every generated case: exit 1 with a diagnostic, and in
  `--vccd` mode the server keeps serving).
- [x] **Unbounded argv arrays** [P].  *Done:* the directory lists are sized by
  argc (one per-thread allocation), the depfile table grows until every file
  fits; 5000 `-I`, 5000 `-include` and a 70000-header depfile work.  Was:  `driver/ppdriver.c:410,489-498,561`
  (`dirs_i[1024]`, `all[2048]` …), `write_deps` 65536 files
  (`driver/ppdriver.c:288`).  Allocate from the job arena.
  *Validate:* T5 (5000 `-I`, 5000 `-include`, a TU with 100k headers in the
  depfile).
- [x] **fd handling: uninitialised cmsg, three fds assumed** [R].  *Done:* cbuf
  zeroed, room for 16, count from cmsg_len; not exactly three closes them all.  Was:
  `vccd/vccd.c:172,186,875-877`.  Zero `cbuf`, take the count from `cmsg_len`,
  reject requests without exactly three fds.
  *Validate:* T12 (send 0, 1, 2, 4 fds; server rejects, does not close
  unrelated fds — check that the listening socket and other open connections
  survive).
- [x] **fds leak into children** [R].  *Done:* received fds get FD_CLOEXEC,
  spawn uses POSIX_SPAWN_CLOEXEC_DEFAULT (closes the macOS race), O_CLOEXEC
  in fixdep/elf/ppdriver.  No child-fd test yet.  Was:  Use `MSG_CMSG_CLOEXEC` (Linux; on
  macOS set `FD_CLOEXEC` immediately under a lock or spawn with
  `POSIX_SPAWN_CLOEXEC_DEFAULT`), `O_CLOEXEC` in `driver/ppdriver.c:743`,
  `vccd/fixdep.c:407`.
  *Validate:* T12 (a spawned child lists its open fds: only 0-2).
- [x] **No timeouts; idle connections stall all workers** [R].  *Done:* a worker
  waits 2 s (poll) for the request, SO_RCVTIMEO bounds the payload; a failed
  recvmsg no longer spins in CMSG_NXTHDR on a zero cmsg_len.  Was:
  `vccd/vccd.c:182,190,305`.  Read the request with a deadline; don't tie a
  worker to a connection until the request is complete.
  *Validate:* T12 (open 2×ncpu idle connections, then a real job completes
  within 1 s).
- [x] **Jobs not cancelled on client exit; SIGTERM exits mid-job** [R].  *Done:* a
  job whose client's connection reads EOF writes no output (checked before
  each write and the rename); the work itself still runs to the end.
  vccd-test killed-client-no-output.  (SIGTERM
  done: the listening socket closes and is unlinked, queued and running jobs
  finish, then exit; vccd-test sigterm-drains.  Cancel on client exit open.)
  `vccd/vccd.c:979,1017`.  Detect peer close (kqueue EV_EOF / poll POLLHUP) and
  abandon the job; on SIGTERM stop accepting and drain.
  *Validate:* T12 (kill the shim during a long TU: the output file is not
  written; SIGTERM during a job: the job completes or its output is absent).
- [x] **Outputs written in place** [R].  *Done:* every output (`-o`, `-S`, deps)
  goes to `NAME.vccPID.N.tmp` and is renamed when complete; a failed job
  unlinks it and leaves the old file.  Was:  `driver/ppdriver.c:266`.  Write to a
  temporary next to the target and `rename()` (vccd/ar.c already does).
  *Validate:* T12 (interrupt during write: either the old file or none,
  never a truncated one with a new mtime).
- [x] **Request environment and umask ignored by in-process jobs** [R].  *Done:*
  jobs read `CPATH` (after -I), `C_INCLUDE_PATH` (after -isystem) and
  `SOURCE_DATE_EPOCH` (UTC __DATE__/__TIME__/__TIMESTAMP__) as clang, from the
  request's env under vccd (`ppd_env.envp`); outputs follow the client's umask.
  vccd-test request-env-reaches-job.  The VCC_*/VPP_* debug knobs stay
  server-wide.
- [x] **Caches never invalidated** [R].  *Done:* a build is a client process
  group (the shim sends getpgrp(); a shell gives each make its own): a request
  from a new group waits for running jobs, then the file, directory and
  include caches are emptied (header/decl caches key on the file entries, so
  they follow).  Within one build nothing is revalidated: a header regenerated
  mid-build after it was read is still stale.  vccd-test: 4 tests XPASS.  Was:  Token cache keyed by path, caches
  misses (`lex/lex.c:1484-1505`, comment at `pp.c:4025`); dircache; header
  cache replays on top.  Either scope all caches to a build (make's
  jobserver lifetime / an explicit generation the shim sends) or validate
  with (dev, ino, size, mtime_ns) on each first use per build and drop
  dependent header-cache regions.
  *Validate:* T12 (compile, edit a header, compile → new object; create a
  previously missing header → found; `__has_include` flips; regenerate a
  header during a build).
- [ ] **Memory only grows** [R].  (server side done: at a new build, idle, a vccd
  above `VCC_RESTART_MB` resident (6144) answers what it holds VROOM_STATUS_RETRY
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
  concurrent shims, one server).  An explicit VCC_SOCK is never started.  Was:  `vccd/shim.c:96-99`.  Autostart the server
  (with a lock) or run the job in-process in the shim.
  *Validate:* T11 (no server running: `vcc -c a.c` succeeds).
- [x] **Request size capped at 1 MB** [R].  *Done:* 8 MB (above Linux's ARG_MAX
  with an 8 MB stack); larger still gets the shim's "request too large";
  vccd-test request-2mb.  Was:  `protocol.h:13`.  Long command
  lines plus env exceed it.  Stream or raise with a clear error.
  *Validate:* T12 (a 2 MB env/argv works or fails with a message).

## Tier 3: features general C needs

### Codegen (`emit.c`)
- [ ] **PIC/PIE** [P]: `-fPIC`/`-fPIE` only set macros; `sym_needs_got`
  `emit.c:2362`.  GOT for preemptible symbols under `-fPIC`, direct for
  local/hidden, PIE default on hosted Linux as clang.
  *Validate:* T6 (`ADR_GOT_PAGE`/`LD64_GOT_LO12_NC` for extern data under
  `-fPIC`, none for `static`/hidden); T8 link a shared library + an executable
  using it, run under qemu-user; kernel vDSO built by vcc and boot test.
- [ ] **ELF TLS** [P] (`emit.c:8883`): local-exec / initial-exec / TLSDESC
  per `-ftls-model` and PIC, `.tbss`/`.tdata`.
  *Validate:* T6 (relocs per model); T1 run under qemu with the runtime's
  TLS setup (`tests/rt` start.S sets `tpidr_el0`); T3 already has TLV.
- [ ] **CFI / `.eh_frame`** (and Darwin compact unwind) [P].
  `-fasynchronous-unwind-tables` default on hosted Linux.
  *Validate:* T6 (`.eh_frame` present, `llvm-dwarfdump --eh-frame` valid
  for every function); T3 `unwind.c` (`_Unwind_Backtrace` from a deep vcc
  call chain lists every frame; a C++ exception thrown through vcc frames,
  built with clang++ for the C++ part).
- [ ] **DWARF** (`-g`): line tables first, then variables/types.
  *Validate:* T6 (`llvm-dwarfdump --verify`); `llvm-symbolizer` maps
  addresses to the right lines for `tests/vcc`; later a scripted lldb
  session (break at file:line, print local).
- [x] **Register spilling** [P] (done: a third try of the function spills each binary
  operator's left value to a frame slot; tests/vcc/deep_expr.c, deep_mixed.c (depth 10..200,
  long/calls/__int128/double/long double), stress expr-right-nest-250/10k): ~18-deep right-nested expression was a
  fatal "out of registers" (`emit.c:2176-2195`).
  *Validate:* T1 `deep_expr.c` (generated: right-nested depth 10..200,
  mixed int/FP/calls); T5 for depth 10k (no crash).
- [ ] **`_Float16`** (`emit.c:3520`), **FP vectors** (`float32x4_t`,
  `emit.c:5886`), **`"w"` asm operands** (`emit.c:7831`), **16-byte atomics**
  (`emit.c:5205`), **128-bit overflow builtins** (`emit.c:4925`),
  **`__atomic_fetch_nand`**, **HVA / half-float HFA** (`hfa_walk` `emit.c:2526`).
  *Validate:* one T1 file each (`f16.c`, `neon-float.c`, `asm-w.c`,
  `atomic128.c`, `ovf128.c`); T2 for HVA/half HFA.
- [ ] **Stack protector, BTI/PAC (`-mbranch-protection`), `-pg`,
  `-fpatchable-function-entry`**; `-finstrument-functions` is parsed but
  unused in emit.
  *Validate:* T6 (canary load/check, `paciasp`/`bti c`, `__fentry__`
  call, nops + `__patchable_function_entries`); T1 for a canary-smash
  detection under qemu.
- [ ] **Fixed limits** (~~128 call arguments~~ fixed in 42ecfb3, 256 nested loops/switches, 256
  cleanups, 16 tracked constants, 16M instructions / 4M labels per function).
  *Validate:* T1 `limits.c` (generated: 300 arguments, 1000-deep nested
  loops, 1000 cleanups); T5 for the extremes.
- [x] **`-S` on Darwin writes ELF directives** (`.type @function`).  (Done: emit/darwin.c
  rewrites the text into Apple's dialect; far b.cond/cbz in text are long form on both
  targets.  `bench/vcc-darwin-s.py`: 1428 vcc + gcc-torture tests, -S assembled by `as`,
  same result as the direct object.)
  *Validate:* T3: `vpp -S` output assembled by `as` (Apple) for the vcc
  suite.

### Front end (`cc.c`)
- [ ] **C23 / per-TU `-std`** [P] (partly done: per-TU keyword map `kwmap` (C23 spellings ->
  GNU keywords; true/false/nullptr/constexpr), `#if true`, digit separators through a
  translated copy (vlex_digitsep: `-E` and `#x` print them without the `'`, clang keeps them),
  tests/vcc/c23_keywords.c, c99_ids.c.  Left: `u8'a'` (one token only in C23: the lexer
  cache needs a per-mode entry, which would also give exact separators), nullptr_t,
  constexpr checks, `_BitInt`, `#embed`): one global keyword table (`kwclass`
  `cc.c:740-768`), `o->std` never read.  Per-TU keyword set; then `bool`,
  `true`/`false`, `nullptr`, `constexpr`, `typeof_unqual`, single-argument
  `static_assert`, digit separators (lexer), `enum : type`, `_BitInt`,
  `= {}`, `[[...]]` standard attributes, `#embed` (pp).
  *Validate:* T1 `c23-*.c` with `vcc-flags: -std=c23`; T4 that the same
  keywords are identifiers under `-std=c99` (`int bool = 1;`).
- [ ] **Builtins**: ~~`__builtin_shufflevector`, `__builtin_convertvector`~~ (done:
  tests/vcc/vector_builtins.c; vector diagnostics spell clang's unnamed form, without the
  `(vector of N 'T' values)` note and typedef names) [P].  Library builtins (clang's `F`
  ones, 479) are declared from cc/libbuiltins.inc (gen-libbuiltins.py) as calls to the
  function: `bench/vcc-builtins.py` 473 -> 302 unknown of 924 for aarch64 Linux;
  `__has_builtin` is also 1 for what the front end implements (pp_builtin_known, set by
  cc): 337 -> 237 answer 0, none 1 where clang says 0 (the harness fails on that).  Audit the full list against clang's `Builtins.td` for the target
  (generate a list, try each).
  *Validate:* T1 per builtin; `bench/vcc-builtins.py` that compiles a
  generated use of each clang builtin and reports unknowns.
  (Partly: `__builtin_clzg`/`ctzg`/`popcountg`, `__builtin_ldexp*`,
  `scalbn`, `trunc*`, `round*`, `floorf`/`ceilf`, `fmaxf`/`fminf` added for
  `make -C ccs self-check`; the generic forms reject a fallback whose
  argument has side effects.)
  ~~`__has_builtin` answers 0 for names absent from the kernel/SDK~~ (done for what vcc
  implements, see above; the rest are builtins vcc lacks).
- [ ] **SVE in the front end**: `arm_sve.h` types, `target("+sve")`.
  *Validate:* T1 under `qemu -cpu max`, layout checks for the types.
- [ ] **`ext_vector_type` silently dropped; unknown attributes no warning** [P].
  `attr_names` `cc.c:5052`.
  *Validate:* layout `_Static_assert(sizeof(float4)==16)`; T4/diag for
  `-Wunknown-attributes`.
- [ ] **Attributes without effect**: `ifunc`, `tls_model`, `alloc_size`,
  `format`, `nonnull`, `deprecated`, `warn_unused_result`, `returns_twice`,
  `target`.
  *Validate:* T6 for `ifunc` (STT_GNU_IFUNC) and `tls_model`; diag tests
  for `format`/`nonnull`/`deprecated`/`warn_unused_result` warnings.
- [ ] **Blocks (`^{}`)** for Darwin headers.
  *Validate:* T3 compile programs including `<dispatch/dispatch.h>` and
  calling a block.
- [ ] **Scope details**: K&R parameter-list tags (`cc.c:7341`),
  declarations after labels (`cc.c:7886`).
  *Validate:* T1 small cases; T4 for the error cases.
- [ ] **Fixed limits** (struct members now 1M, pointer levels 1024, nested
  declarators stack-guarded): 4096 struct members (`cc.c:6255,6313`), 64
  declarator levels (`cc.c:6047-6113`), `DOPS_MAX` 4096 (`cc.c:5551`), 16
  nested anonymous members (`find_member` `cc.c:3310`), 256 K&R params
  (`cc.c:7339`).
  *Validate:* T1 `fe-limits.c` (generated, at 2× each limit).

### Preprocessor and lexer (`pp.c`, `lex/lex.c`)
- [x] **`#pragma once` by path string, capped at 4096** [P].  (Done: by (dev, ino) from
  read_src, list grows; symlink/hard link/`a/../a` checked against clang -E -P;
  tests/vcc/once.c, once_tri.c.)
  `pp.c:4189,5191,4479`.  Key on (dev, ino); unbounded set.
  *Validate:* `ccs/fuzz/regress/pp/` style files + `make -C ccs fuzz-diff`
  extended with a directory fixture (symlink, hardlink, `a/../a/x.h`),
  compared with `clang -E -P`.
- [x] **UTF-8 BOM not skipped** [P].  *Done:* read_file blanks a leading BOM
  (columns kept); tests/vcc/bom.c with bom_inc.h.  (vlex_get_hook inputs,
  fuzz-pp, are not filtered.)
- [x] **Trigraphs** under strict `-std=c89/c99/c11/c17` or `-trigraphs` [P].  (Done:
  `vlex_trigraphs` lexes a translated copy; `-ftrigraphs`/`-fno-trigraphs` too; tests/vcc/trigraphs.c.
  No `-Wtrigraphs` warnings yet.)
- [x] **No `//` comments in C89** (`-ansi`, `-std=c89`): clang lexes `//` as two `/` there [P].
  (Done: `vlex_c89` copy, strict c89 only as clang; tests/vcc/c89_comments.c.)
  *Validate:* pp-diff fixtures with each `-std`.
- [x] **`SOURCE_DATE_EPOCH`; `__TIMESTAMP__` from file mtime** [P].  (Both done:
  __TIMESTAMP__ is the current file's mtime, `??? ...` when stat fails, as clang.)
  `pp.c:3168-3178`.  *Validate:* T11 (fixed epoch → exact strings).
- [x] **`\U` outside the BMP printed unconverted in `-E`** [P].  (Done: identifiers' UCNs
  print as UTF-8 (not in asm mode, not in literals); `##` accepts UCNs; regress/pp/ucn-ident.c.)
  *Validate:* pp-diff fixture.
- [ ] **`#include` in macro args accepted** [P].  (`-E` output on errors
  done: printed to stdout with status 1, no file with `-o`, as clang;
  checked by the error row of `bench/vcc-driver.py`.)
  *Validate:* T4.
- [ ] **`#embed` / `__has_embed`** [P].  *Validate:* pp-diff fixtures plus
  T1 (`unsigned char d[] = { #embed "bin" };` checksums).
- [ ] **Output modes ignored**: `-C`, `-CC`, `-dD`,
  `-traditional-cpp` (reject); line markers without `-P`; text identical to
  clang's spacing.
  *Validate:* `bench/vcc-diff.py` new mode `text` comparing bytes (not
  tokens) for `-E` with and without `-P`.  (`-M`/`-MM` done: see Depfiles;
  `-H` done: `bench/vcc-diff.py hdr` 912/912, header cache off under `-H`.)
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
- [ ] **Options**: `-imacros`, `-isysroot`/`--sysroot`, `-nostdlibinc`,
  `-iprefix`/`-iwithprefix`, `-mmacosx-version-min`, unknown `-std=` values
  (error), `iso9899:199409`.
  *Validate:* T11 rows; predefine diff (`-dM`) for `-std` and
  `-mmacosx-version-min`.
- [ ] **Predefines for other targets and flags**: only two tables;
  `--target=x86_64`, `-m32`, `-funsigned-char`, `-fstack-protector*`,
  `-ffast-math`, `-march=+sve`, `-pthread`, `-fsanitize=*`, `-fexceptions`
  change nothing.  Generate predefines from (target, flags) using a table
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
  large (2 GB or more)"; an #include of one still says not found) (`lex/lex.c:1373`, `uint32` offsets
  `lex/lex.h:50,61`).  Clear error.
  *Validate:* T5 (sparse 3 GB file → "file too large").
- [ ] **x86-64 host build**: `lex/lex.c:4` NEON, `lex/lex.c:1517` `yield`,
  `lib/intern.c:1` `arm_acle.h`.  Scalar fallback (lexref.c exists) or SSE/AVX2
  port; CRC32 via SSE4.2.
  *Validate:* CI job building and running `fuzz-regress`, `vcc-diff.py cmp`
  on x86-64 Linux (cross-target aarch64); `fuzz-lex` already compares NEON
  vs scalar, add SSE vs scalar.

### Assembler (`as/as.c`, `arch/a64/as/enc.c`)
- [ ] **`.cfi_*`** → `.eh_frame` [P].  *Validate:* T7 (clang `-S` output
  with CFI assembles, `.eh_frame` byte-identical).
- [ ] **`.loc`, `.file N`, DWARF line tables, `.uleb128`/`.sleb128`** [P].
  *Validate:* T7 with `-g`.
- [ ] **Scalar FP** (fadd … frint*, `fmov #imm`) [P].  *Validate:* T7
  encoding tables.
- [ ] **AdvSIMD arithmetic, long/wide/narrow, by-element, ld2-ld4/st2-st4,
  single-lane ld1/st1** [P].  *Validate:* T7 encoding tables (generated
  from llvm-mc's AArch64 instruction list for these classes).
- [ ] **Integer aliases** sxtw, uxtb, sxt*/uxt*, cls, ngc, mneg, smnegl,
  CSSC [P].  *Validate:* T7 tables.
- [ ] **Pointer auth, barriers/hints** (paciasp … xpaclri, csdb, esb, ssbb,
  pssbb, dgh, `psb csync`, wfet, chkfeat, sysl, prfum, ldapur, stlur), **MTE
  irg/addg** [P].  *Validate:* T7 tables.
- [ ] **SVE/SVE2/SME** (operand parser for `z`/`p` registers first) [P].
  *Validate:* T7 tables; T7 `-march=armv9-a+sve2` corpus.
- [ ] **Directives**: `.comm`/`.lcomm`, `.symver`, `.altmacro`,
  `.float`/`.double`, `.dc.*`, `.eqv`, `.weakref`, `.end`, `.err`,
  `.addrsig`/`.addrsig_sym`, `.variant_pcs`, `.tlsdesccall`, `.nops`,
  `.balignw`/`.p2alignl`, `.base64`, `.inst.n`, COMDAT `"G"` sections
  (`as/as.c:722`), `@gnu_unique_object` (`as/as.c:1125`), full `.reloc` (`as/as.c:836`).
  Silently ignored today: `.print`, `.warning`, `.ident`, `.arch`, `.cpu`
  (`as/as.c:2307-2440`).
  *Validate:* T7 per directive; `.arch` must gate instructions (T4).
- [ ] **Relocation operators** (TLS `:tlsdesc:`, `:tprel_*:`,
  `:gottprel*:`, `:dtprel*:`, `:prel_g*:`, literal `ldr x0, :got:v`)
  (`mod_kind` `as/as.c:1912`, `obj/obj.h:53-72`) [P].  *Validate:* T7 with
  `-fPIC` and TLS sources.
- [ ] **Mach-O syntax** `@PAGE`/`@PAGEOFF`/`@GOTPAGE…`; vasm writes ELF
  only.  *Validate:* T7 with `--target=arm64-apple-darwin` vs clang's Mach-O
  (`llvm-objdump --macho -r -d`).
- [ ] **Wrong output**: GOT relocs against local symbols converted to
  section+addend [P]; integer branch targets as `*ABS*` relocs; `adr x0, 4`
  rejected (`arch/a64/as/enc.c:1005`).  *Validate:* T7 / T6 relocation comparison.
- [ ] **Diagnostics**: line numbers off by one (`as/as.c:575/636/133`), `# N
  "file"` markers ignored (`as/as.c:579`), EOF errors at `:0:` [P].
  *Validate:* T4 for the assembler (`// expect-error: x.S:2:`).

### Linker (`ld/ld.c`) and binutils
- [ ] **ELF input parsing unchecked** [P]: `parse_obj` `ld/ld.c:1670-1697`,
  `fill_isecs`, `st_shndx` (`ld/ld.c:1847,2521,2887`), `add_archive`
  (`ld/ld.c:1609-1655`, long names `ld/ld.c:1633`), reloc width (`ld/ld.c:2987`),
  objcopy `elf_open` `ld/objcopy.c:113`, misaligned headers in archives
  (`ld/ld.c:1672`), SHN_XINDEX / `e_shnum == 0` (`ld/ld.c:1684,1850`).
  *Validate:* T9 `fuzz-elf` (ASan) + `regress/elf/` with the probe's
  corrupt objects (bad `e_shoff`, truncated, `st_shndx=0x500`, oversized
  archive member, ABS64 at `size-1`); T8 with a 70k-section object.
- [ ] **Crashes on ordinary inputs** [P]: no script / script without
  `.symtab` (`ld/ld.c:4410`), `/DISCARD/` of a merge section (`ld/ld.c:643`).
  *Validate:* T8 cases `no-script`, `discard-comment`.
- [ ] **Options**: `-znotext` swallowed (`ld/ld.c:3315`), `-m X` unchecked
  (`ld/ld.c:3296`); rejected `-pie`, `-static`, `-L`/`-l`, `-e`,
  `--gc-sections`, `--dynamic-linker`, `--version`, `--emit-relocs`
  (`ld/ld.c:3295`); ignored `--start-group` (`ld/ld.c:3284`), `--eh-frame-hdr`
  (`ld/ld.c:3285`), `-z now/relro/noexecstack`, `--orphan-handling`.
  *Validate:* T8 per option; unknown options are errors (T11-style table).
- [ ] **Relocations**: GOT (`ADR_GOT_PAGE`, `LD64_GOT_LO12_NC`) + relaxation,
  TLS LE/IE/TLSDESC + relaxations + `PT_TLS`, MOVW_*, CONDBR19, TSTBR14,
  PREL32 to undefined (`ld/ld.c:2560-2570`).
  *Validate:* T8 (each against lld, run under qemu where runnable).
- [ ] **Archives**: lazy member resolution, `-L`/`-l`, groups (`ld/ld.c:3342`);
  COMMON (`ld/ld.c:1783`); COMDAT groups (`ld/ld.c:1693`).
  *Validate:* T8 static link of a musl `libc.a` hello world (run under
  qemu-user); a C++ template object pair.
- [ ] **Default entry** `_start` when no `ENTRY()`.  *Validate:* T8.
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
- [ ] **Diagnostics**: undefined symbol with section+offset and all
  referencing objects (`ld/ld.c:4231`); `-r` output differs from lld.
  *Validate:* T4-style expected-stderr for vlink; T8 `-r` byte diff.
- [ ] **var**: only thin archives (`vccd/ar.c:286,352,672`), ops `r`/`m`/`t`,
  32-bit GNU symtab only; ld skips `/SYM64/` (`ld/ld.c:1622`).
  *Validate:* `bench/var-cmp.py` extended to regular archives and `x`, `d`,
  `q`, `p`, `s`; round-trip with llvm-ar.
- [ ] **vobjcopy / vnm**: missing `--strip-debug`, `--add-section`,
  `--only-keep-debug`, `-O elf64-*`, `--add-gnu-debuglink`,
  `--set-section-flags`, `--redefine-sym`, `-I`/`-B`; nm `-u`, `-D`,
  archives, unknown long options taken as files, >64 files dropped
  (`ld/objcopy.c:1039`).
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

### Driver (`driver/ppdriver.c`, `vccd/vccd.c`, `vccd/caps.c`)
- [ ] **vccd compiles only `-c … x.c` for aarch64 Linux; the rest goes to
  clang** (`vccd/vccd.c:460-485`, input must be the last argument).
  *Validate:* T11 (`-c a.c -o a.o`, flags after the input, `-o` before
  `-c`) all handled by vcc (a counter in vccd's stats, or `VCC_NO_FORWARD=1`
  that turns forwarding into an error).
- [ ] **No linking, no crt/sysroot discovery** (`vccd/caps.c:174`).
  *Validate:* T11 `vcc hello.c -o hello` on aarch64 Linux (qemu-user) and
  macOS; then an autoconf project (e.g. GNU hello) and a CMake project
  configure and build with `CC=vcc`, tests pass.
- [ ] **Unknown flags silently ignored** [P] (`driver/ppdriver.c:435-533`): `-g`,
  `-fsanitize=*`, `-m32`, `-march=znver4`, `-x c++`, `.cpp` inputs, `@rsp`,
  several inputs; input detection by `strstr(a, ".c")` (`driver/ppdriver.c:526`).
  Every option is either implemented, explicitly ignored with a documented
  reason, or an error (`-Wunused-command-line-argument` like clang).
  *Validate:* T11 table generated from clang's option list
  (`clang --help-hidden`): each must be in an explicit table in
  driver/ppdriver.c; a test fails when an option has no entry.
- [ ] **Missing**: `-print-*` (`-print-file-name` echoes, `vccd/vccd.c:665`),
  `-dumpmachine`, `-v`, `-###`, `-save-temps`, `-shared`, `-l`/`-L`, `-Wl,`.
  *Validate:* T11 rows.
- [ ] **caps.c probe answers can contradict real compiles** (`vccd/vccd.c:291`,
  `vccd/caps.c:199-208`), unmatched sources forwarded to clang (`vccd/vccd.c:395`),
  every stdin link probe false (`vccd/vccd.c:353-356`), `.tmp_` heuristic
  (`vccd/vccd.c:252`), `--version` claims clang (`vccd/caps.c:163`).
  Make the table a cache of vcc's own answers (generated by running vcc,
  not clang), key it on target+flags, drop the filename heuristic, report
  an honest version string that CMake/Meson can identify (and a
  clang-compatible `-dM` set).
  *Validate:* `bench/caps-check.py`: for every table entry, run the probe
  for real through vcc with forwarding disabled and require the same
  answer; T11 for `.tmp_` paths compiled for real.

### rsh
- [ ] **`trap` is a no-op** (`vccd/rsh.c:354`); fall back to the real shell
  instead.  **fixdep path match by suffix** (`vccd/rsh.c:414`): require the
  configured kernel tree.  **Wrong errno text** (`vccd/rsh.c:552,568,577`).
  *Validate:* `bench/vsh-test.py` rows against dash (trap, other fixdep
  paths, ENOENT/EACCES/ENOTDIR messages).

## Tier 4: quality

- [ ] **Optimizer**: register allocation of locals, FP values in v
  registers (`f_arith` `emit.c:3673`), jump tables / binary search for
  `switch` (`gen_switch` `emit.c:7179`), fewer caller-saved spills
  (`spill_live` `emit.c:2713`).  Today ~clang -O0 speed, 3-4× slower than
  -O2; stage-2 vcc build 2.6× slower than the clang-built one.
  *Validate:* correctness by all of T1/T2/T10 and the kernel boot;
  performance by `bench/vbench.py` and a small benchmark set
  (`bench/micro/`: matmul, hash, interpreter, the vcc self-build), numbers
  in the plan.
- [ ] **Error recovery**: every `expect()` failure is fatal (80 sites + 32
  `fatal`), missing `;` after a struct ends the TU, typo correction cascades,
  duplicated errors (`void v;`, compound literal in parens).
  *Validate:* `tests/diag/` cases with several independent errors, exact
  clang output (`vcc-diag.py`).
- [ ] **Warnings as errors not controllable**: `conv_error` (`cc.c:2622`)
  always errors for `-Wincompatible-pointer-types`.
  *Validate:* diag tests with `-Wno-error=…` and `-Wno-…`.
- [ ] **Missing warnings** (only 7 `-W` flags): `-Wunused-variable`,
  `-Winteger-overflow`, `-Wexcess-initializers`, `-Wreturn-stack-address`,
  `-Wconstant-conversion`, `-Wmultichar`, `-Winvalid-noreturn`,
  `-Wuninitialized`, `-Wunknown-attributes`, `-Wdeprecated-declarations`,
  `-Wformat`, `-Wunused-value`; pp `-Wextra-tokens`,
  `-Wexpansion-to-defined`, `-Winvalid-pp-token`, `#pragma GCC warning`.
  *Validate:* one diag test per warning; `vcc-sema.py warn` stays 879/879
  (no false positives on the kernel).
- [ ] **Invalid code accepted**: duplicate enumerator / parameter,
  `&register_var`, `_Alignas(3)`, `typeof` on a bitfield, `*(void *)p` as a
  value, `return v();` of void from int function, `-fsyntax-only` missing
  initializer-constness errors (`static int s = g();`, `5 % 0`).
  *Validate:* T4 each, then promote to diag.
- [ ] **`"abc"[1]` rejected as a static initializer**; vector types print as
  `''` in messages (`tyname`).  *Validate:* T1; diag test.
- [ ] **Diagnostic details**: columns on `##` and invalid directives,
  `#line` file names ignored, nested macro notes (one level today), the
  "expanded from macro" note only for undeclared identifiers.
  *Validate:* diag tests.
- [ ] **Longer fuzz runs** and pp-diff over the whole fuzz corpus (PLAN-fas2
  "Status fuzzning").  *Validate:* runs recorded in the plan, findings in
  `regress/`.
- [ ] **PostgreSQL completeness**: TAP tests (`-Dtap_tests=enabled`) not
  run; links still by ld64.  *Validate:* `bench/pg/build.sh` with TAP,
  same pass set as clang.
- [ ] **One set of data structures in `lib/`** (restructure 2026-11-09).
  Merged so far: `vreserve` (4 copies), `vregion` (as + object writer),
  `vbuf` (pp diagnostics + fixdep).  Still side by side in `lib/`: the
  buffers `vbuf`/`vsbuf`/`vzbuf`/`vfixbuf`/`vfdbuf` (they differ in OOM
  behaviour: abort, sticky flag, bool) and `tbuf.h`; the arenas `vregion`
  (offsets) and `varena` (pointers); the hashes `vhash_obj`/`vhash_strtab`.
  Still in their component, tied to its state: ppdriver's `buf` (first
  16 MB from a per-job vmreg), cc's `vec` (inline storage, frozen into
  `c->lists`), ld's block arena (process-wide list, generations), as's
  `.req` hash, objcopy's StrTab and the object writer's symbol table.  One
  OOM policy for code inside vccd (fail the request, never abort) decides
  the buffer.  *Validate:* outputs byte-identical (objects, `-S`, `-E`),
  `bench/vbench.py gate`.
- [ ] **The linker as a split module** (restructure 2026-11-09).  `ld/ld.c`
  is one TU of ~4400 lines over process-wide globals (`objs`, `isecs`,
  `gsyms`, ... of anonymous `VVEC` types); its arm64 parts are fragments
  (`arch/a64/ld/*.inc`) it #includes.  Make it files by topic with an
  `ldimpl.h` (named vector types, VPRIV globals), as cc/pp/emit, and the
  fragments real files.  *Validate:* `bench/vbench.py check --base` (the
  vmlinux link byte-identical), `bench/vobjcopy-cmp.sh`, gate.
- [ ] **Target-OS code out of the generic files** (restructure 2026-11-09).
  `platform/` has whole files only (Mach-O writer, ELF writer); the
  target-Darwin branches (`DARWIN(c)` and `PP_DARWIN` tests, ~25) are still
  inline in `emit/tu.c`, `emit/type.c`, `emit/data.c`, `arch/a64/emit/`
  (call, value, func, builtin, lval, abi), `cc/tu.c`, `cc/layout.c`,
  `driver/ppdriver.c` and `vccd/vccd.c`.  Give them hooks in `platform/<os>/`
  once the x86-64 backend shows which differences are per OS and which per
  CPU (Apple arm64's ABI is both).  *Validate:* outputs byte-identical,
  `bench/vcc-abi.py --host`.
