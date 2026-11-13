# vcc 1.0: first release

**Goal:** vcc is released as a fast C compiler that can be used directly via
`CC=vcc`, under the MIT license.  It targets those who want a faster
compiler with modern error messages.  The kernel build (vroom) is our
reference user, not the product.

**Decisions (2026-11-09, round of questions with Johan):**
- Product: vcc as a C compiler (vcc/vas/vld + vccd).
- Quality bar: TODO.md tiers 1-3 empty (tier 4 may remain as known limitations).
- Language: C up to C23, with GNU extensions.
- Code: reasonable -O1 (register allocation, simple inlining, constant propagation).
- Diagnostics: better than clang; clang parity is the floor.
- Platforms: macOS arm64, Linux arm64, cross-compiling mac->Linux, Linux x86-64
  (ELF, SysV), macOS x86-64 (Mach-O) and wasm.
- Drop-in: `CC=vcc` is enough.  configure, meson and cmake get through, and
  unknown gcc/clang flags give a warning, not an error.
- vccd: default and transparent.  The shim starts the server itself.
- Speed: published comparison against tcc and clang.
- Distribution: source code, prebuilt tarballs, Homebrew, Debian/apt, container.
- License: MIT.
- Governance: the release is driven by criteria, not dates.  The agents work as
  now.  The gates below are the common goal.

## Gates

The release is done when all gates are green on all platforms the gate
applies to.  Each gate must have a script with a clear green/red result
(see G0).

| # | Track | Gate (measurable) |
|---|-------|-------------------|
| G0 | Gate rig | `bench/release-gate.py` runs G1-G9 (or reads their latest results) and writes a status table per platform; CI runs it on every platform |
| G1 | Correctness | TODO.md tiers 1-3 empty, no `xfail` left that points there. Csmith and YARPGen differentially against clang: N CPU hours per target and optimization level without a diff |
| G2 | Corpus | The Linux kernel boots Debian to `running`; PostgreSQL `meson test`, SQLite (the TCL tests) and vcc self-built (stage 2 = stage 3) green; the Debian selection builds and passes its own tests with `CC=vcc` |
| G3 | Drop-in | autoconf, meson and cmake configure with `CC=vcc` for the whole corpus without project patches. Unknown flags give a warning. Flag table against gcc and clang (`bench/vcc-driver.py` extended) |
| G4 | Platforms | G1-G3 run on every target: macOS arm64, Linux arm64, mac->Linux arm64, Linux x86-64, macOS x86-64, wasm. ABI tests (`bench/vcc-abi.py`) clang/vcc × caller/callee on every target |
| G5 | Code quality | `-O1`: run time within X % of clang -O1 on a fixed benchmark suite; `-O0` unchanged in compile speed (the SSA path `-O2` is owned by `PLAN-vir.md`; G5 measures `-O1` and stays with the non-SSA optimizer) |
| G6 | Diagnostics | `bench/vcc-diag.py` without regressions against clang, and a suite of its own with cases where vcc is provably better than clang (spans through macros, fix-its, explanations) |
| G7 | Speed | `bench/` publishes compile time per TU and for whole projects: vcc against tcc and clang -O0 (vcc -O0) and against clang -O1 (vcc -O1), both cold in its own process and with vccd. The numbers are in the README |
| G8 | vccd as default | TODO.md tier 2 empty. Autostart, version check between shim and server, fallback to a plain process if vccd is missing or crashes. `vccd-test.py` + `vcc-stress.py --vccd` run for hours; works in CI and containers |
| G9 | Packaging | A tag yields via CI: source tarball, binary tarballs per platform, Homebrew formula, .deb, container image. README, LICENSE (MIT) and THIRD-PARTY with origin and license for the clang tables (`caps.c`, `ppdata*.inc`), gcc c-torture and tcc tests2 |

## Order

The order follows dependencies, not dates.  Tracks that do not touch the
same files can run in parallel.

1. **G0 gate rig and CI.**  It makes status visible to all agents.  First
   the existing test scripts are collected, then new gates are added as they
   exist.
2. **G9 legal.**  LICENSE, THIRD-PARTY and a check that generated tables and
   imported test suites may be distributed under the MIT project.  It is
   cheap now and expensive later.
3. **G1 tier 1 + differential fuzzing**, in parallel with the **x86-64 backend**
   (`arch/x86_64/`; the directory structure is prepared).
4. **G8 tier 2 and vccd as default.**
5. **Tier 3** (front end, C23, driver flags for G3).
6. **G5 -O1**, only when tier 1 is empty.  An optimizer on top of incorrect
   code makes the bugs harder to find.
7. **The wasm target, the Debian selection (G2), G6's own suite and G7's measurements.**
8. **G9 packaging**, and then the tag.

## Open questions

- **wasm:** does it mean a wasm backend (wasm32 or wasm64, WASI?) or vcc
  run as wasm (`contrib/wasm/`, already exists)?
- **Full C23:** `_BitInt` with arbitrary width, `#embed`, `constexpr`: the whole
  standard or clang's level?
- **-O1:** which benchmark suite, and which X in G5?
- **The Debian selection:** which packages and how many?  A fixed list or top N
  by popcon among pure C packages?
- **"Better than clang":** who judges that?  A curated suite or an A/B test
  with users?
- **vccd as default:** behavior in CI and containers, and the socket's
  security model on shared machines.
- **Name:** "vcc" collides with Microsoft's VCC and the verifier VCC.
- **Gate owners:** should each gate have a responsible session?
- **Csmith/YARPGen:** how many CPU hours is N?

## Status

### Status 2026-11-11: G2/G7, PostgreSQL on Linux

The PostgreSQL harness can now isolate source and build trees on Linux,
prepare the whole command database and check that every compile job really
succeeded before speed is compared.  Fast contract tests run in CI without a
PostgreSQL checkout.  G2 and G7 remain open: the harness alone proves
neither that current main builds PostgreSQL nor that its performance goals
are met.

### Status 2026-11-11: G5/G7, wasm stores across structured joins

At `1d8e29a`, wasm32/64 use the shared budgeted flow solver for private
exact 4/8-byte frame cells.  Typed control events and unique block tokens
describe the flow without text parsing or SSA.  Overwritten stores are
removed across structured joins; operand evaluations and real barriers are
preserved.  Loops, dispatchers/goto, asm, SJLJ and dynamic frames abstain.
Full convergence is required before publication.

114 focused/existing SDK-free level/target/rule checks pass; an
intentionally incorrect barrier merge is rejected on wasm32 and wasm64.
Independent review is done.  The full own SDK suites exit with status 0:
wasm32 178/178 PASS, 36 XFAIL, 16 skips; wasm64 182/182 PASS, 37 XFAIL,
11 skips.  The exceptions do not count as PASS; categories and logs are in
`PLAN-fas2.md`/`PLAN-wasm.md`.  The CI step is added, remote run remains.
Later integration `1458a90` against the exact flow parent `f7327bd` passes
the same 114 checks and 66 byte-identical O0/noopt pairs.  Matched clang 23
v3 builds give +0.314-0.552 % Callgrind Ir in unchanged checks,
+0.106/+0.095 % in a fixed wasm32/64 mix, and +4.494-4.683 % in
concentrated positive diamond tests.  The latter reduce stores 800/1000
to 200 and relocatable objects 2.886-3.325 %.  Active analysis costs more than
1 % there; the result does not replace a full performance gate or measured
program speed.  Exact corpus exceptions and method are in the plans.  G5/G7,
general DSE and full O2 remain open.
### Status 2026-11-11: G1/G4, semantic follow-up milestone on top of the native stack

Four separate fixes build on the real Git parent `74f6ec4`:
lifetime of declarations after labels, independent nested GNU rank lists,
C90's digraphs/statement scope with mixed cache jobs, and full width in
generic rotations.  No complete C23 record compatibility changes are included.

Unity and file-by-file build: own x86 tests 235/235 with six reference exceptions;
ARM/QEMU 240/240 with one existing xfail; ABI 33/33 per target/build;
diagnostics 225/225.  Kernel sema run/batch 887/887 on both builds.
Full PP parity is 920/920 in cmp/batch both before the change
on the parent and after (unity).  Four targeted unchanged GCC cases pass per target/build;
the C90 oracle runs seven modes and 21 mixed cache jobs.  Five focused runtime tests
pass also at O2/O3.  ASan/UBSan pass diagnostics, three new runtime tests and
the C90 matrix on both Linux targets.  Wasm64 preflight: 191 pass, 39 existing
xfail, 11 reference exceptions; no XPASS markers needed changing.

Nine selected Kefir cases pass all combinations on x86; ARM gives five passes
and four explicit Clang reference rejections (wchar/wide type contract).
This is targeted revalidation, not a new full S4/S5 run.  Artifacts,
bundle with preserved Git history and manifest: `work/results/semantic-stack-74/`.
G1/G4 are still open.

### Status 2026-11-11: G1/G4, rotations over 128 bits

On base `a41361e`, the operand's and the count's full width is kept before modulo
for generic rotations.  GCC's unchanged `builtin-stdc-rotate-1.c` keeps
`-std=c11`; its 510-bit part runs only on x86 (ARM advertises max 128).
Targeted verification: unity/separate-file build, O0/O2/O3 with types, side effects and
large counts, plus semantics run/batch.  Result:
`work/results/stdc-rotate/validation.json`.  Clang's limitation for dynamic
rotations over 128 bits is documented separately; it is not a pass.
The whole of S4 has not been rerun and G1/G4 are still open.

### Status 2026-11-11: G1/G4, lifetime of declarations after labels

At `a41361e`, the front end's synthetic scope around declarations after
labels is fixed.  This eliminates overlapping live ARM stack slots and
premature cleanup also on x86.  Kefir std_attributes18 passes all four
combinations on both Linux targets and builds; own suites, sema and diag
are green with existing exceptions.  See `TEST.plan` S5b and
`work/arm-computed-goto/` for delimited results.  G1/G4 remain open.

### Status 2026-11-11: G1/G4, combined native stack on top of 1c5b5d1

Seven separate native fixes have been combined and validated at the exact
stack top `1c5b5d1`.  Unity/file-by-file: own Linux suites x86 232/232 and ARM
237/237 with existing reference exceptions/xfail, ABI 33/33 and atomics 3/3.
PP parity cmp/batch is 920/920; sema run/batch 887/887 on both builds.
Six focused Kefir cases pass all four combinations on x86;
ARM's three active cases pass while two Clang rejections and the x86 guard
are reported separately.  ExtInt and alloca edge cases and GCC pr113596 are green;
the earlier x86 assertion failure for wide strings is now fixed.  See `TEST.plan`
S5b and `work/native-stack64/` for exact evidence and remaining limits.
No full Kefir/GCC gate or further platform coverage is claimed;
G1/G4 are still open.

### Status 2026-11-11: G1/G4, lifetime of inlined alloca

At base `103cdee`, the stack is restored at the inlined function's common
return point on Linux x86/ARM, after the arguments are evaluated.  The caller's VLA
save points are not changed by the inlined function's alloca.  GCC's unchanged
`gcc.dg/pr113596.c` keeps `-O2`; the regression covers nested inlining,
early returns and live VLA/alloca in the caller, also at `-O0`.
Results from unity/separate-file build, own run suites and semantics run/batch:
`work/results/pr113596/validation.json`.  The whole of S4 has not been rerun;
G1/G4 and other targets remain separate open gates.

### Status 2026-11-11: G5/G7, wasm frame-cell forwarding

Wasm32/64 now use the shared cell/value core for exact
4/8-byte frame cells and remove overwritten stores.  Typed
emission events preserve address/RHS effects and the operand stack; no
SSA graphs or assembler text patterns.  A positive aggregate test goes from six
to two memory operations.  An eleven-file mixed test gives +0.599 % host Ir,
controls +0.18-0.30 %, positive test +7.03 %.  This is delimited data,
not the whole of G7.  16 focused run/code requirements, sanitizers, independent review,
6120 identical native results and 149 own wasm32 PASS verify the step.
A local wasm64 reference crash is baseline-reproduced and visible; see
PLAN-wasm.md.  X86's adapter remains experimental because of cost;
narrow wasm cells, general unread-DSE, full O2 and G5/G7 remain.

### Status 2026-11-11: G1/G4, object size on Linux

At base `9d022c3`, the object size analysis distinguishes declared subobjects from
raw allocation and padding.  `-fstrict-flex-arrays=0..3` is part of the cache identity;
mode 3 keeps the declared minimum size.  `counted_by` on stable pointer fields
uses the current counter in dynamic modes 0-2; volatile bases/fields are left
unknown without a read.  Inlining preserves bounds only for direct, unwritten and
non-aliased pointer parameters with side-effect-free arguments.

Verification covers unity/separate-file build, own run suites on Linux x86-64
followed by ARM64/QEMU, five unchanged GCC cases (`pr101836_1/2/3`, `pr39343`,
`pointer-counted-by-8`), independent strict-flex/volatile/allocation probes and
the kernel's semantics in both process and batch mode.  Exact results and binary hashes:
`work/results/objsize-main-validation.json` and
`work/results/objsize-main-independent/results.json`.
Both builds: ARM 185/185, semantics run/batch 887/887, GCC 5/5 per target;
34 independent configurations gave 102 matching runs (272 commands).
Original x86 run: 179/179 and six skips, of which `branch_far` was a
Clang reference timeout after 60 s, not an approved compiler result.
A separate supplement uses PR37 harness `761142b` and its explicit
300 s contract: 1/1 without skips on both targets with both builds.  Results are saved in
`work/results/objsize-main-branchfar/results.json`.  Original logs are not changed.

Remaining: general flow/alias analysis, transitive inline parameters,
`alloc_size` contracts and dynamic expressions with side effects; unknown bounds
should stay unknown.  The whole S4 corpus and other targets have not been rerun
for this milestone.  Targeted Wasm32/64 probes go 10/10 with one run XFAIL
for `object_size_inline`: the same test gives byte-identical assembly and the
same runtime error with the earlier `main`; the Clang reference passes.  The test
needs Wasm inlining/argument bounds and is not skipped.  G1/G4 are not done.

### Status 2026-11-11: G5/G7, copies and direct extension proof

A shared native copy core reuses arm64's analysis; x86 does only a
candidate search in its existing two-address walk, without its own allocations
or liveness passes.  A separate extensions.c omits provably redundant
unsigned32 normalization from the preceding write.  148 focused
run combinations green after integration with main9d022c3, including
volatile/high-bit/signed regressions; both build modes and sanitized
contracts verified.  500-function v3 instruction probes against 14379fe give
+0.23% loop and -0.74% control for the combination.  The profile's reach
is limited; vbench/native full corpus and the whole G5/G7/O2 goal remain.
### Status 2026-11-11: G1, wide BitInt on Linux

Scalar `_BitInt` over 128 bits now has limb-based code generation for
Linux x86-64 and arm64, with the existing fast paths for at most 128 bits
preserved.  The milestone covers integer arithmetic, comparisons, conversion
between integers, bit-fields, generic bit counts, constant expressions and replay.
Vector elements over 128 bits and switch bounds outside the signed 128-bit
representation are explicitly rejected.  Floating-point conversion follows below.
This does not complete C23 or G1.

Validation of the integration at `ae941cb`: unity and separate files,
own x86 tests 184/184 with five reference exceptions and ARM 189/189.  Independent
safety and rejection probes pass 36/36 across both build modes and
targets.  The kernel's sema passes 887/887 individually and in batch for both
build modes.  Wide arithmetic and bit counting pass twelve widths 129-7001,
the constant tests 128 values plus positive/negative contracts, replay eight
translation units on two targets and wide ABI 24 shapes in four directions
per target and build mode (384 runs).  Diagnostics: 220/220 per build mode.
Optimizer rules O0-O3 and register effects with ASan pass.
Full commands and results are in
`work/results/bitint-integration/`.  ARM's Clang 23 lacks native support for
widths over 128; ARM's wide ABI tests use documented C aggregates
as proxy and must not be described as native Clang BitInt parity.

**Reintegration on main `2dc4ea3`:** both build modes pass own
Linux x86 222/222 with six reference exceptions and ARM 227/227 with one run
existing XFAIL.  Full own Wasm64 suite: 180/180, 37 run existing
XFAIL and eleven reference exceptions.  Kernel sema is 887/887 in both run modes and
builds.  Independent BitInt probes are 36/36; today's leaf-frame, cell,
copy and constant check gates pass on both builds.
Runtime conversion to/from floating point is still explicitly
outside this integration.  Exact commands, results and source manifest:
`work/results/bitint-main2dc/`.  The full vendored Linux run corpus
then passes 1942/1942 x86 (130 exceptions) and 1945/1945 ARM/QEMU
(126 exceptions and one existing run XFAIL), without cache reuse.
All of Kefir's 875 runtime pairs have also been run, first on x86, then ARM:
x86 572 approved (+91), 8 vcc failures and 10 signal/timeout; ARM 455 approved,
3 vcc failures and 7 signal/timeout.  No earlier approvals are lost;
reference failures and unsupported are unchanged and do not count as pass.
Binary SHA256 `f2a6f04804de1393357a2b4341059f8bc9cbc30c8b5361808df43f203d6e0b74`.
G1/G2 and the rest of the test plan remain open.


### Status 2026-11-11: G1, padding in BitInt expressions

Compound assignments now normalize the old `_BitInt` value before
shift, division and remainder on both Linux x86-64 and arm64.  The unchanged
Kefir case `bitint_assign_shift1` passes all four Clang/vcc pairings
on both targets.  The regression tests 9-128 bits, dirty padding, sign and
single evaluation of operands.  Unity and separate files and the kernel's
sema 887/887 individually and in batch are verified for the isolated
fix.  The combined milestone `04209a5` passes 18 unchanged Kefir pairs
with four compiler combinations per Linux target (144 runs), both
build modes, diagnostics 220/220, ABI 32/32 per target and ASan 19/19 per target.
Own ARM tests: 189/189; x86: 183/183 plus a separate approved `branch_far`
with PR37's reviewed 300 s contract (the original 60 s timeout is preserved in
the log).  Five other x86 reference exceptions remain.  Wasm64 goes 148/148 with
31 run XFAIL and ten reference exceptions; focused Wasm32 5/5 with seven XFAIL.
The padding test's Wasm gap is proven with both earlier baselines.
Manifest and final gate: `work/validate-kefir-7eca264/validation.json` and
`final-manifest.json`.  G1 remains open.

### Status 2026-11-11: G5/G7, register locals on x86

Common eligibility selection in `codegen/opt/locals.c` is used by all three
backends; x86 now keeps profitable scalars in four callee-save registers.
Cold probes are unchanged; a native loop test goes 7.830->6.345 ms but
a division-free loop corpus costs about 11 % more compiler CPU.  That is
not an approved 1 % gate.  Snapshot/ABI safety for builtin setjmp and
late/inlined aligned frames is tested.  Both build modes: 116 focused
combinations; x86 own suite 137/137, arm64 142/142, cross-ABI 22/22.
CI now runs x86 probes under qemu-user.  See OPT3 in `PLAN-fas2.md` for
hashes, measurement method and remaining work.  The G5/G7/O2 goal remains open.

### Status 2026-11-11: G5/G7, shared stack cell analysis

The next part now shares value/store facts in `codegen/rec/slot-values.c`.  6108
byte-identical arm64 results and separate sanitized contracts verify
the move.  X86/wasm hookup and a combined cost gate are in progress; G5/G7
remain open.

Arm64 now uses a common analysis of exact allocation cells,
overlap and address escape in `codegen/rec/slots.c`.  6069 byte-identical
object/text comparisons, 177 own arm64 run tests and a sanitized
interval contract verify the move.  Two limited instruction profiles
show +0.655/+0.303 % compiler work; method and bases in OPT5 in
`PLAN-fas2.md`.  X86/wasm hookup and shared rewrites remain;
G5/G7 and full O2 coverage are not done.

### Status 2026-11-11: G5/G7, direct parameter binding

Selected x86 parameters are bound directly to register homes without extra ABI spill
or reload.  Narrow types, stack arguments, mixed GP/FP, hidden sret
and varargs verified at O0-O3; cross-ABI 22/22.  Paired short measurements and
limitations are in OPT3 in `PLAN-fas2.md`.  The G5/G7/O2 goal remains open.

### Status 2026-11-11: G5/G7, unreachable code and signed powers of two

A shared linear record pass removes safely unreachable suffixes; x86 now has
register/effect contracts for the next copy/liveness step.  Signed constant
power-of-two division/remainder shares one recipe between all backends; wasm
keeps compact div/rem where clang does too.  Separate rule files
and target adapters.  Both build modes pass 96 focused checks;
own suites x86 135/135 and arm64 140/140.  Synthetic compiler CPU cost
and remaining uncertainty are reported at OPT8 in `PLAN-fas2.md`.
G5/G7 and the whole O2 goal remain open.

### Status 2026-11-11: G5/G7, shared branch cleanup

Arm64 and x86 now use `codegen/rec/fallthrough.c`; arm64's extraction
is byte-identical for 131 existing test files with O1 and -g.  Wasm
omits redundant while branches with a separate reachability proof.  Both build
modes pass 64 focused code/run combinations; own suites x86
133/133 and arm64 138/138.  Measurement data and remaining CPU uncertainty are
at OPT2 in `PLAN-fas2.md`.  G5/G7 and the O2 goal remain open.

### Status 2026-11-11: G5/G7, shared integer identities

`codegen/opt/identity.c` now shares type-correct integer identities between all
backends; the power-of-two rule lives separately in `divpow2.c`.  Unity and
file-by-file pass 32 code/run checks at O0-O3.  Own suites: x86
131/131, arm64 136/136.  Effects and narrow/high register bits are covered by
regressions.  Measurement noise prevents conclusions about small compiler CPU differences;
see OPT1 in `PLAN-fas2.md`.  G5/G7 and the whole O2 goal remain open.

### Status 2026-11-11: G5/G7, first shared rule implemented

Unsigned constant power-of-two division/remainder is now lowered through the same helper
for x86, arm64 and wasm32/64.  All 16 target/level combinations pass
code requirements and literal-based run checks, also file-by-file.  Own suites:
x86 130/130, arm64 135/135.  The wasm test path uses vcc's own objects
and linker plus the system's Node 24/25, without Emscripten; full
runtime replacement is W9 in `PLAN-wasm.md`.  Short before/after measurements
and the limitations of hardware counters are documented at OPT1
in `PLAN-fas2.md`.  This is partial progress; G5/G7 remain open.

### Status 2026-11-11: G5/G7, common implementation plan

`PLAN-fas2.md` **Shared optimizer without SSA** now owns OPT0-OPT8 after
a code review of all three generators.  C rules/strategies are shared by x86,
arm64 and wasm32/wasm64; arm64's existing post-optimizer is extracted for
the register targets.  X4 and the wasm plan own hookup and target lowering.
The plan states dependencies, effect/ABI/memory contracts, bounded CPU cost,
tests and separate measurements of compiler and generated code.  The next
delivery is a new base and a shared integer rule with three consumers.
No optimization implemented; G5/G7 remain open and G5's X is missing.

### Status 2026-11-11: G5, audit of x86 code quality

`PLAN-x86.md` X4 has a prioritized audit against clang 23.1.6 and GCC 15.2.0
on Ryzen 7 9700X: local optimizations with low compiler cost without
IR/SSA.  Extended with 29 reproducible probes at O0-O3, baseline and
native: 24/24 runs give the same checksum and pass limit values and
code checks for existing properties.  Flags, pass logs and code are saved
by `bench/vcc-optaudit.py`; vcc O1/O2/O3 give the same assembly here.
No optimizations implemented or measured
run-time goal; G5 remains open and the benchmark suite's X is not fixed.

### Status 2026-11-11: x86 function targets, SSE4 and math
G1/G2 have a coherent x86 feature model without LLVM at run time:
CPU/native, function attributes, inlining, SSE4 string operations and interrupt.
AVX ABI for wide vectors and records is diagnosed until the transport exists.
Fast-math lowers sqrt and limited integer powers with preserved evaluation.
Isolated validation on base 0757803: x86 162/162 (five reference/target skips),
ARM64 167/167, ABI 23/23 per target, PP 920/920 and sema 887/887 in both modes.
Feature macros 587/587, attribute/ABI guards 302/302, SSE4 gates 124/124
and GCC pr40470 4/4 pass.  G1/G2 and S4 are still open.
### Status 2026-11-11: atomic whole objects on Linux
Larger atomic records, integers and complex values now use the libatomic ABI
when the built-in 1/2/4/8-byte path is not enough.  Compare-and-swap loops keep
the whole object and the right expected value also with padding and contention.
Pthread tests and atomics regressions pass Linux x86 and ARM in unity,
file-by-file build and ASan/UBSan; kernel sema is 887/887 in both run modes.
The new fast host rig runs in normal and ASan CI.  Atomic vector types
remain in the front end; G1/G2 remain open.

### Status 2026-11-11: object representations and packed bool vectors
`__builtin_bit_cast` has run-time and byte-based constant evaluation with unknown
padding; packed bool vectors have logical elements, bit storage and verified
Linux/Wasm ABI.  Isolated unity/file-by-file validation is 164/164 own x86 tests,
169/169 ARM tests and 32/32 ABI tests per target.  TSan and guard-page tests are
green; see `TEST.plan` S2 for limits and validation.  This moves G1/G2 but
does not close them; separate `nullptr_t` identity remains.

### Status 2026-11-11: external tests on Linux x86-64
ARM64's stack allocation and slots now use 64-bit offsets with checked
overflow handling.  GCC `20031023-{1..4}` compile (4/4); `frame_wide`
runs with 64 KiB under QEMU and compiles with 1 TiB both directly to object
and via assembly text.  Isolated own ARM64 suite: 115/115.  This moves G1/G2
but does not close the gates.

G1 has got measurable baselines from clang's diagnostics tests, GCC's
compile suite and two small run suites.  Failures and crashes remain; see
`TEST.plan` S2, S3 and S5.  G1 and G2 are not done.

### Status 2026-11-09: plan written, no gates built
None of G0-G9 exists as a script yet.  What exists today and becomes parts of
the gates: `vcc-diff.py`, `vcc-sema.py`, `vcc-ctests.py`, `vcc-abi.py`,
`vcc-diag.py`, `vcc-driver.py`, `vccd-test.py`, `vcc-stress.py`,
`bench/pg/`, `vccd-kbuild.sh` + `boot-test.py`.  What is missing: README,
LICENSE, x86-64 backend, wasm backend, -O1, SQLite corpus, Debian selection,
Csmith/YARPGen rig and packaging.

### Status 2026-11-11: G5/G7, switch membership with bitmask

OPT6 recognizes limited stacked cases with a common destination and
lowers them to a range-guarded bitmask on arm64, x86 and both wasm targets.
Targeted run and code checks pass O0-O3 and `VCC_NOOPT`; the vcc suite
passes 246/246 with one existing xfail.  A targeted x86 example shrinks from
131 to 85 bytes.  The measurement's limitations and remaining work are in
PLAN-fas2; G5/G7 and full O2 remain open.

### Status 2026-11-11: G5/G7, balanced switch dispatch

OPT6's first part uses common strategy selection/sorting and balanced
comparison trees on x86, arm64 and wasm; dense wasm tables are kept.  The combination
with b63c269 builds unity/file-by-file and passes 164 targeted run/code requirements,
219 arm64 and 214 x86 regressions.  On the fded15a base, 500-function checks
are byte-identical and extra Callgrind compile work is at most +0.180 %; a
targeted x86 switch test reduces executed instructions 48.39 %.  Delimitation,
raw data and remaining OPT6 parts are in PLAN-fas2.  G5/G7 and full O2 are open.


### Status 2026-11-11: G5/G7, cheaper shared cell collection

Arm64/wasm use the shared streaming interval collector without changed
code generation.  Matched O1/O2 micro cases reduce the whole compilation's Ir by
0.145-0.453 % for several fields; one field costs 0.050-0.053 % and controls
0.006-0.071 %.  Randomized sanitizer/mutation oracle, 118 byte-parity pairs,
observable accesses and focused wasm run cases pass.  Details and
rejected variants are in PLAN-fas2.  No broad performance gate or new
optimization rule is included; G5/G7 and full O2 remain open.
### Status 2026-11-11: G5/G7, unsigned32 reciprocal division

OPT8's u32 part uses common reciprocals for native quotient/remainder and
limited wasm quotients.  180 targeted run/code requirements, sanitized math and
pressure contracts, unity/file-by-file and 220 arm64/215 x86 regressions pass.
Against exact 781ca69, extra work in controls is under +0.442 % and a
configured 10-12-file mix between -0.017 % and +0.018 %.  A saturated
arm64 division corpus, however, costs +19.54 % to compile; the trade-off and
delimited x86/wasm run tests are in PLAN-fas2.  This does not close G5/G7.
Signed/u64 reciprocals and full O2 coverage remain.

After integration with main54580f7: the same compiler code, 180/180 targeted cases,
221/221 arm64 regressions (one existing xfail), 216/216 x86 regressions
(six reference skips) and sanitized reciprocal/pressure contracts pass.

### Status 2026-11-11: G5/G7, unsigned64 reciprocal division

OPT8 now has separate u32/u64 recipes, native high product with preserved implicit
registers and a cheap wasm u64 comparison quotient.  196 targeted cases, sanitized
math/register contracts, unity/file-by-file, 222 arm64/217 x86 regressions,
ABI and unwind pass.  Against exact 046c6dc, control/loop tests cost at most
+0.00021 % and the configured mix -0.00794...+0.00082 %.
Saturated u64 functions, however, cost +14.10 % x86/+17.99 % arm64 to compile;
a delimited seeded x86 run test reduces median child CPU 69.55 % with the same answers.
Trade-off, exact hash values and limitations are in PLAN-fas2.  Signed,
more expensive wasm classes and full O2 remain; G5/G7 are not closed by this substep.

### Status 2026-11-11: G5/G7, frameless x86 leaves

Truly frameless x86 leaves can now skip the rbp prologue/epilogue; policy and
limited effect check live in a separate frame.c and the already existing
post-walk.  Explicit frame-pointer policy and mixed declaration caches
are tested.  CFI/debug, hardening entries and a real signal unwind through a
frameless leaf are verified; other functions keep their frame.  Own x86 tests
215/215, ABI 32/32 and existing register-home unwind tests are green.
Two cold 501-function profiles give -0.176 %/-0.537 % Ir; the control's assembly
is identical.  Source/binary hashes, flags and byte limitations are in PLAN-x86.md.
No general speed or program run-time gain is proven.  G5/G7 and the whole O2
remain open; parameter homes and broader measurements remain.

### Status 2026-11-11: G5/G7, pure constant control decisions

OPT6's common control helper is used by x86, arm64 and wasm with
each target's existing `constant_p` policy.  The new substep preserves
O0/noopt in 64 byte comparisons on four targets, passes 164 existing and
16 new targeted level/target checks as well as a file-by-file build.  Two earlier
wasm xfails (`if_call_const`, `cond_dead_and`) are mandatory on wasm32/64.
Global volatile/atomic loads and exactly one evaluation are checked
explicitly.  Against exact 781ca69 with equal host flags, delimited
500-function controls are within +0.095 % extra Callgrind work; the positive
control test reduces the compiler's instructions 2.75-18.36 % depending on
target.  This does not replace a full performance gate or CI and does not show any
general improvement in the program's run time.  G5/G7 and full O2 are open;
raw data, remaining parts and limitations are in PLAN-fas2.

### Status 2026-11-11: integration of x86 function targets and OPT3

Function targets and packed bool ABI now share classification with the optimizer's
register homes.  SSE4 records have explicit register/flag effects; fixed
variadic bool parameters are verified also via indirect calls.  Both build modes
pass own Linux suites (176 x86, 181 ARM), ABI 32 per target, 220 diagnostics tests
and 887 kernel TUs in both sema modes.  G1/G2/G5 remain open.


### Status 2026-11-11: G5/G7, single-operation recipe for multiplying by 3/5/9

A shared constant-time recipe selects separate native adapters: scaled LEA on
x86 and shifted ADD on arm64, O1+ without VCC_NOOPT.  Wasm keeps mul according to the
reference's cost choice.  O0/NOOPT/overflow parity, modular sanitizer/mutation oracle,
post effects, two build modes and four targets' O0-O3 run cases pass.  Code-shape gain
is proven; matched micro cost against historical base 69c1bd7 is at most +0.126 %
for controls; the positive test costs +0.353 % x86 and -2.147 % arm64.
Wasm is byte-identical.  This is the compilation's Ir, not run-time gain or a full
performance gate; final integration CI remains.  Factors
6/7/10/15 and broader arithmetic rules are open.  G5/G7 and full O2 remain open.

After conflict integration with main4bb6d52 via the OPT3 head 9b18f64: unity build,
16/16 targeted unsigned32 cases on four targets and sanitized reciprocal/pressure
and register-effect contracts pass.  Both plans' status is kept.

After mainf97d16d (#47/#52/#55): unity, 16 unsigned32 target/level cases,
16 constant control cases and sanitized reciprocal/register-effect contracts
pass again; the two plans' independent status blocks are preserved.

U64 integrated with mainf97d16d/PR51head54c8f3f: 196 rule tests, 16 control tests,
229 arm64/224 x86 regressions, ABI, unity/file-by-file and sanitizer contracts
pass.  New exact parent measurement54c8f3f->d8957b3: fixed mix at most
+0.01955 %, saturated u64 corpus +13.55 % x86/+17.97 % arm64 compiler work;
delimited x86 run test -69.26 % child CPU.  Historical numbers above and full
open G5/G7 gates are kept; see PLAN-fas2 for hash values and the tests' limits.

After main2dc4ea3 (#50's x86 leaves): unity, 16 division/level/target cases,
sanitized reciprocal/register-effect contracts and leaf-frame policy, cache,
debug/hardening and signal unwind tests pass.  Both status blocks are kept.

### Status 2026-11-11: G5/G7, limited native tail calls

A common semantic check and separate target adapters replace simple direct
return-call wrappers with an epilogue followed by a symbol branch on x86 and arm64 at
O1-O3.  Flag order, inherited attributes, ABI registers, CFI and arm64's direct
object relocations are verified.  Standard wasm keeps a normal call,
as does clang without the explicit tail-call feature.  Kernel sema/parity on the integrated
main base, own x86/arm64 tests, both native ABI suites and real unwind are green.
Two exact 501-function Ir profiles show -12.082 % for the wrapper case and
+0.338 % for an unchanged arithmetic control; only the wrapper case reduces text.
Hashes, scope and remaining exceptions are in OPT8 in PLAN-fas2.md.
This proves no general speed gain.  G5/G7 and full O2 remain open.

U64 has also integrated main2dc4ea3 via PR51head15551a7: 196 targeted cases,
sanitized contracts and the whole leaf-frame harness pass.  The reciprocal files are
unchanged; the measurements remain bound to their earlier exact hash values.

### Status 2026-11-11: G5/G7, signed32/64 reciprocal division

OPT8's signed part on top of `f585591` has common typed reciprocals and
separate x86/arm64 adapters.  Quotient and remainder follow C's rounding
toward zero, also for negative divisors and INT_MIN; wasm keeps the engine's signed
division/remainder.  A proven equality case removes an unnecessary numerator addition for
`/3` among others.  O0, `noopt` and high register pressure keep the earlier path.

Unity/standalone implementations, 16 target/level runs and four noopt
checks pass, as do sanitized math/pressure/register-pair contracts.
Native gives 230 arm64 PASS (one existing xfail), 225 x86 PASS (six visible
reference exceptions), ABI 32 per target and x86 debug/unwind.  Paired v3 measurements
of exact `f585591`->`fbc7105` give a fixed mix of at most +0.00572 % extra
compiler instructions.  Saturated native signed32/64 code costs +14.21...+30.03 %;
a delimited x86 run test gives -63.59 % child CPU, with clang-identical answers.
Eight O0/noopt assembly comparisons are byte-identical.  Full G5/G7 remain open.
The implementation and validation limits are in PLAN-fas2's signed status.


OPT1 integration check on 60027c7 after the wasm parent 6d60e73:
both build modes, 16 full assembly pairs, 16 target/level run cases, sanitized
recipe/post contracts and 36 policy/overflow parity comparisons pass.
The earlier compiler cost still applies to historical 69c1bd7 to f92b67b;
it is not attributed to this new parent.  Integration CI and full G7 remain.

### Status 2026-11-11: G1, promotion of bit-field expressions

Narrow unsigned bit-fields keep their promotion width through assignment,
compound assignment, comma and prefix increment.  `sizeof`/`typeof`,
postfix and explicit casts keep their separate declared type rules.
This fixes two unchanged Kefir contracts on Linux x86-64 and ARM64/QEMU,
all four Clang/vcc pairings.  The regression also tests side effects.
Unity/separate files, sema run/batch 887/887 and own run suites
223/223 x86 and 228/228 ARM respectively are verified; six x86 reference exceptions
and one existing run ARM XFAIL are reported separately.  The whole Kefir suite and
G1/G2 remain.  Evidence: `work/agents/kefir-bitfield/results/evidence.txt`.

### Status 2026-11-11: G1/G2, string prefixes, embed and VLA bounds

A late encoding prefix now governs the whole sequence of adjacent string literals and
preceding escape sequences.  C23's quoted `#embed` searches the working directory
after the includer's directory and before explicit embed directories.  Foldable
array bounds with side effects are evaluated as VLAs instead of dropping the calls.
Four unchanged Kefir cases pass all Clang/vcc combinations on x86;
ARM/QEMU passes embed and VLA, while two string cases are rejected by Clang
because of upstream's signed-wchar assumptions.  Portable string regressions
pass ARM.  Unity/separate files: own x86 224/224 with six reference exceptions,
ARM 229/229 with one existing run XFAIL, diagnostics 224/224 and 40 embed
oracle comparisons.  The kernel's sema 887/887 and full PP parity 920/920
pass both per process and in batch.  G1/G2 remain open.
Frozen source and verification basis: `86c7352`,
`work/results/frontend-milestone-2dc/`.

### Status 2026-11-11: G1, wide integers to/from floating point

Linux conversions use the whole integer magnitude with guard/sticky bits
and one round to nearest even, and decoding of the floating-point
exponent and significand with truncation toward zero, respectively.  No run-time Clang or
128-bit truncation of integer input is used.  Dynamic rounding modes
are not included; this does not complete G1.

On the private milestone `7a1691f`, 6871 exact x86 conversion tests
and 5667 ARM tests pass over five integer widths 129-7001, with six and five
floating-point formats respectively.  Unity and separate files give the same result.  Clang deviations
for BF16, x87 and unsigned binary128 are recorded separately; vcc must always
match the independent Python oracle.  Unchanged Kefir `bitint_cast_fix2`
passes four x86 combinations and two ARM combinations with a vcc callee;
Clang does not support the ARM type's width.  Snapshot/replay passes with ASan/UBSan
after a separate fix of an existing zero-length `memcpy` with a null source.
Verification basis: `work/results/bitint-float-reviewed/`.

The reintegration on `b0b6107` preserves the newer constant and alias optimizations.
Unity and separate files pass the same IEEE matrix, 222/222 own x86 tests
(with six existing reference exceptions), 227/227 ARM tests (one existing xfail,
no skipped tests) and the kernel's sema
887/887 both individually and in batch.  The IEEE matrix also passes with `-O3`
on both targets.  The rules O0-O3, constant-control, leaf-frame, slot-values,
slot-observable, switch-plan and debug lines pass in both build modes.
ASan/UBSan pass IEEE, constant tests and snapshot/replay on both targets.
CI now runs the floating-point matrix and sanitizer replay.  Evidence:
`work/results/bitint-float-main-reviewed/`.

The import on top of PR #60 keeps its full Linux baselines and changes
only the floating-point follow-up.  Both unchanged x86 Kefir cases
`bitint_cast_fix2` and `bitint_typeconv6` are now 2/2 approved with four
compiler combinations each; the corresponding standard ARM run
reports two Clang reference rejections.  The wide ARM value proof
comes from the IEEE matrix and the separately reported vcc callee calls.
G1/G2 and strict floating-point environment remain open.

Full Kefir reruns on the frozen stack `a41361e` give 585 approved
x86 cases and 461 ARM/QEMU cases, without vcc compile errors.  Six and four
runtime/timing failures respectively and all reference/unsupported categories are kept;
G1/G2 remain open.  Linux ASan/UBSan goes 232/232 x86 and 237/237 ARM
with existing exceptions.  Wasm64 preflight goes 189/189 with 38 run XFAIL;
the ExtInt test covers the already verified BitInt ABI gap on Wasm32/64.
Exact commands, unchanged compiler code and the test marker's parent test:
`work/results/native-stack64-ci-preflight/`.

G7: a profile-driven emitter step reduces repeated identifier and x86 register
matching.  The fixed instruction gate and the targeted tests are in
PLAN-fas2.md; actual compile time on whole projects remains to be measured.

The OPT1 extension with power-of-two multiplication and bit complement is implemented
for x86, arm64 and wasm and has focused O0-O3 validation and a small
matched compiler cost profile in PLAN-fas2.md.  G5/G7 and general O2 coverage
remain open.

Limited switch value tables have been validated on four targets; synthetic
compiler costs and their limitations are in PLAN-fas2.md.  G5/G7 and
a realistic composite cost gate remain.

The composite optimization stack with value tables passes the limited
1 % compiler cost gate on four targets (PLAN-fas2.md); the saturated
switch cost and the release gates G5/G7 remain to be followed up.

The byte output of switch value tables has been sped up after `perf` profiling;
matched measurements and their limitations are in PLAN-fas2.md.  G5/G7 remain.

The arithmetic and switch stack that previously ended up in feature branches is now
integrated on main.  Four targets' focused tests, native suites and the current matched
compiler instruction gate are reported in PLAN-fas2.md.  G5/G7 remain.

Type bounds written as limited pure constant expressions are now simplified on the
four targets.  Tests and the delimited compiler instruction measurement are in
PLAN-fas2.md; G5/G7 and general O2 coverage remain open.

Wasm lowers integer equality with zero and unsigned `x <= 0` to a direct
zero-test instruction.  Four-target tests and delimited compiler cost are in
PLAN-fas2.md; G5/G7 and general O2 coverage remain open.

Delimited identities for the same ordinary local integer variable are simplified
on all targets.  Four-target tests and compiler instruction measurement are in
PLAN-fas2.md; G5/G7 and general O2 coverage remain open.

Self-comparisons of ordinary local integers are also handled by the common
identity recipe, with a NaN regression and four-target measurement in PLAN-fas2.md.

Double unary integer operations are simplified commonly for all targets; tests and
delimited compiler cost tests are in PLAN-fas2.md.  G5/G7 remain.

A first budgeted value table reuses safe identical sibling expressions
on all targets; tests and delimited compiler costs are in PLAN-fas2.md.
General local CSE, G5/G7 and full O2 coverage remain.

A local integer initialization can be reused in a directly following pure return
on all targets; verification and delimited costs are in PLAN-fas2.md.
General local CSE and G5/G7 remain.

The budgeted local value reuse also covers later pure
integer initializations; four-target tests and delimited cost are in PLAN-fas2.md.

The limited local value scan can pass independent local writes
with invalidation of dependencies; tests, cost and a saturated control case are in
PLAN-fas2.md.  General CSE and G5/G7 remain.

Budgeted local value reuse can also be used in pure assignments;
four-target tests, costs and limitations are in PLAN-fas2.md.  G5/G7 remain.

Local value reuse from PR #97 and #100 is integrated on main; combined
four-target tests and compiler costs are in PLAN-fas2.md.  G5/G7 remain.

VIR's verification of slot exposure has a safe linear analysis for long
graphs; test and cost are reported in PLAN-vir.md.  A separate parser finding and
VIR's remaining V1-V7 as well as G5/G7 remain.

### Status 2026-11-12: G4 basis, x86-64 through VIR

The opt-in SSA path has a first x86-64 lowering of SysV leaf functions
and SSE2 loops.  `bench/vcc-vir-x86.py` requires real lowering, verifies
the allocation and runs the assembler and own object paths; unity/nonunity and
ASan/UBSan/TSan green.  GCC 1615/1615, TCC 105/105 and own 282/282 go with VIR;
inline has the same five failures with SSA off and on.  This is a new platform basis
for G4, not a closed gate: calls and broader ABI/corpus coverage remain.
Measurements and remaining work are owned by `PLAN-vir.md` V2/V4.

### Status 2026-11-12: G2/G4, real Linux corpora through x86-VIR

TSVC gives 151/151 checksums and all 82 candidates reach x86 machine code;
SQLite builds and speedtest1's hash is clang's, zlib passes self-test and
byte/CRC comparison.  PostgreSQL/psql and CPython are now also compiled
as corpora: 13 of 1504 and four of 334 C jobs respectively are rejected on named
builtin gaps (TODO T13); CPython additionally has link errors, even without VIR.
PostgreSQL's explicitly labeled mixed build gives 81 passed/0 failures/249
skipped tests and does not count as green G2.  No gate is closed by these
subsets.  Coverage, per-function retreats and measurements in PLAN-vir.md.
