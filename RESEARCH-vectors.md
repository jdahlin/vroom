# Vectors in VIR: representation, allocation, and a first loop vectorizer

Research of 2026-11-12 for `PLAN-vir.md`. Sources: two web research passes
(IR documentation and source trees, ABI documents, vectorizer documentation
and papers) and one pass over `ccs/vcc/vir/`, `ccs/vcc/emit/vir/` and TSVC_2
built on this machine. A claim carries the tag of the page it was read on; a
point the research could not confirm on the page is marked *(unverified)*.
Local numbers are from the M1 Pro, Homebrew clang 23.1.4, TSVC_2 at commit
911fc63 with `iterations` lowered from 100000 to 100.

The question (Johan): how should vector support be added to VIR, with
auto-vectorization that passes TSVC_2 as the first user?

Short answer: **lane-typed 128-bit vector types in the existing type byte,
the scalar opcodes reused where the operand kind is "same type as the result",
masks as all-ones integer lanes, and vectors in the existing float register
class with 16-byte spill slots and every v register caller-saved.** That is
Cranelift's scheme, and it is the one that costs VIR least: the instruction
stays 16 bytes, the opcode table stays the one description the printer, parser
and verifier read, and the allocator needs a slot size per value rather than a
third class. Sections 5 and 6 are the proposal; sections 1 to 3 are what it
rests on.

`SPEC-vir.md` 1.3 excludes vector types from 1.0 and 1.4 names loop
transformations a non-goal, so this is a change of scope, not a detail: see
section 7.

## 1. How other IRs represent vectors

### 1.1 Cranelift: lane-typed values, polymorphic opcodes

- Types are `i8x16`, `i32x4`, `f32x4`, ...; the lane count is a power of two
  [CL-IR].
- Opcodes are polymorphic over scalar and vector through type variables
  (`Int` is "any scalar or vector integer type"); derived variables tie the
  operand and result types. A shift takes one scalar amount for all lanes
  [CL-IR] [CL-INS].
- A vector `icmp`/`fcmp` returns an integer vector of the same shape with -1
  in each lane where the condition holds [CL-INS].
- Cranelift once had boolean types (`bN`, `bNxM`). They were removed, with
  `bconst`, `bint`, `breduce` and `bextend`; scalar compares now give `i8`
  0/1 and vector lanes -1/0 [CL-5031]. The stated reason: loading and storing
  bools was "a somewhat incongruous hole in our semantics", and wide bools
  were only ever useful as vector masks [CL-3205].
- `select` takes a scalar condition and picks a whole value; `bitselect`
  picks per bit [CL-INS].
- `splat`, `extractlane`, `insertlane` (lane index an 8-bit immediate),
  `vall_true`/`vany_true`, `vhigh_bits` (a scalar bit mask) [CL-INS].
- `shuffle` carries a 128-bit immediate mask; `vconst` names a constant-pool
  entry, not an inline immediate [CL-INS].

**VIR takes:** the whole scheme. It fits `vir_ins.ty` (one byte) and the
opcode table's `OK_T` operand kind as they are.

### 1.2 WebAssembly SIMD: one type, the lanes in the opcode

- "The `v128` value type is the only type introduced"; every opcode says how
  it reads the lanes (`i32x4.add`, `f32x4.mul`) [W-SIMD].
- Boolean vector types were in the proposal and were removed in 2017 because
  the group "was not able to reach a consensus"; compares return a `v128`
  mask and the selects became one `v128.bitselect` [W-22].
- A compare's lanes are all zeros or all ones; the reductions are
  `any_true`, `all_true` and `bitmask` [W-SIMD].
- Float `min`/`max` propagate NaN. `pmin` is `b < a ? b : a` and `pmax` is
  `a < b ? b : a`, which is what SSE's `minps`/`maxps` compute [W-SIMD].
- The operation list is a useful checklist of what a complete 128-bit set
  is: construction (const, splat, extract, replace, shuffle, swizzle),
  integer arithmetic with saturating forms, bitwise with `andnot` and
  `bitselect`, compares, float arithmetic with rounding, conversions
  (convert, trunc_sat, demote/promote, narrow, extend, extmul, dot), memory
  (load, store, load-splat, load-zero, lane load/store) [W-SIMD].

**VIR takes:** the mask rule and the `pmin`/`pmax` definition (the C idiom
`a < b ? a : b` is `pmin`, not IEEE `min`: section 4.4). It does not take the
single type: with one `v128`, every opcode needs a lane variant, and VIR's
table would grow by six rows per operation instead of one type bit.

### 1.3 WebKit B3/Air: one type, the lanes beside the opcode

- B3's type kinds are Void, Int32, Int64, Float, Double, V128, Tuple [B3-T].
- The lane and the signedness are on the value, not the type: a `SIMDValue`
  holds a one-byte `SIMDInfo` (6 bits of lane, 2 of sign mode) and an 8-bit
  immediate [B3-V] [B3-I].
- Opcodes are lane-generic (`VectorAdd`, `VectorEqual`, `VectorBitwiseSelect`,
  `VectorSplat`, `VectorExtractLane`, ...) with a few NEON-shaped ones
  (`VectorZipLower`, `VectorReverse`) [B3-V].
- 128-bit constants are loaded from a data section [B3-C].

**VIR takes:** nothing new. This is the same information as Cranelift's with
the lane moved from the type to a flag; VIR's `fl` word is already spoken for
by per-op flags (comparison code, alignment, NSW).

### 1.4 LLVM IR: `<N x T>` and `<N x i1>`

- Fixed vectors `<N x T>` and scalable `<vscale x N x T>` [LL-REF].
- Vectors of non-byte-sized elements (so `<N x i1>`) are bit-packed, and
  "when `<N*M>` isn't evenly divisible by the byte size the exact memory
  layout is unspecified" [LL-REF].
- The backend's boolean contents are a per-target choice among Undefined,
  ZeroOrOne and ZeroOrNegativeOne, and `getSetCCResultType` picks the mask's
  legal type [LL-TL]. That NEON and SSE use ZeroOrNegativeOne, `<4 x i1>`
  becoming `<4 x i32>`, is *(unverified)*.
- Illegal vector types are split, widened, scalarized or promoted, "until a
  legal type is found" [LL-TL] [LL-CG].
- An undef element of a shuffle mask used to stop poison and miscompiled
  (PR44185); LangRef was changed in 2023 so such an element yields poison
  [LL-D148637].

**VIR takes:** the warning. An abstract `i1` mask and arbitrary `<N x T>`
need a legalizer; VIR has none and should not grow one. Every VIR vector type
is one the 1.0 targets hold in a register, and a mask is the integer vector
the hardware produces. No undef lanes either (R1): `shuffle` indices are
validated immediates.

### 1.5 GCC

- `VEC_COND_EXPR`'s mask is "of signed integral vector type", -1 selecting
  one arm and 0 the other; other values "should never" occur but optimizers
  must not rely on that [GCC-V].
- RTL: `vec_merge`, `vec_select`, `vec_concat`, `vec_duplicate`, `vec_series`
  [GCC-R]. Mask modes for AVX-512 and SVE are *(unverified)*.

**VIR takes:** `vec_series` as the name of the induction-vector constant
(section 5.2).

### 1.6 Compilers without vectors

- QBE: `w l s d` only [QBE]. MIR: I8..U64, F, D, LD, P, BLK [MIR].
- Go's SSA has opaque `TypeVec128/256/512` and a `TypeMask`, for the
  experimental `simd/archsimd` intrinsics of Go 1.26; the lane shape is not
  in the SSA type and there is no auto-vectorizer [GO-T] [GO-B]. The source
  comment on `TypeMask` reads "spilling will be 'interesting'" [GO-T].

### 1.7 Summary

| IR | Vector types | Mask | Operations |
|---|---|---|---|
| Cranelift | lane-typed `i32x4`, `f32x4` | same-shape integer vector, -1/0 | polymorphic opcodes |
| Wasm SIMD | one `v128` | `v128`, lanes all ones/zeros | one opcode per lane shape |
| B3/Air | one `V128` | `V128`, all ones/zeros | generic opcode + one byte of lane info |
| LLVM IR | `<N x T>`, scalable | `<N x i1>`, legalized per target | polymorphic opcodes |
| GCC | vector modes | signed integer vector, -1/0 | polymorphic codes by mode |
| Go SSA | opaque `Vec128` | a separate `TypeMask` | per-shape ops *(unverified)* |
| QBE, MIR | none | none | none |

Nobody who removed boolean vectors put them back.

## 2. ABI and register allocation

- AAPCS64: v0-v7 carry arguments and results; v8-v15 are callee-saved, but
  "only the bottom 64 bits of each value stored in v8-v15 need to be
  preserved"; v16-v31 are caller-saved [AAPCS].
- Apple lists its divergences from AAPCS64 and none concerns the FP/SIMD
  registers [APPLE]; LLDB's macOS arm64 ABI says "v8-v15 are non-volatile
  (and specifically only the lower 8 bytes of these regs)" [LLDB].
- x86-64 SysV: every xmm register is caller-saved [SYSV].
- So a 128-bit value live across a call is spilled on every 1.0 target.
- regalloc2 has three classes (Int, Float, Vector) and one spill size per
  class [RA2]. Cranelift's arm64 backend nevertheless puts scalar floats and
  vectors in one class with 16-byte slots, so an `f32` spill takes 16 bytes,
  and marks all of v0-v31 call-clobbered "because we cannot currently
  represent partial registers to regalloc2" [CL-ABI].
- LLVM has FPR32, FPR64 and FPR128 as classes over aliased registers, so the
  spill size follows the value's class [LL-RI].

**VIR takes:** one float class, as now. `vir_alloc` already treats every v
register as caller-saved (`emit/vir/a64.c`: "v0-v7 and v16-v30, all
caller-saved"; the lowering refuses a callee-saved float register), so the
partial-register problem does not arise. The slot size comes from the value's
type (`vir_tysize`), which is what LLVM's classes achieve, without Cranelift's
16 bytes for a float.

## 3. How loop vectorizers are built

### 3.1 LLVM LoopVectorize

- Four parts: the driver, legality, the planner (VPlan) and the cost model.
  Legality decides, the planner builds one plan per range of vector factors
  and prices them, and only execution changes the IR [LV-SRC] [VPLAN].
- Loop shape: a preheader ending in an unconditional branch, exactly one back
  edge, a latch ending in a branch. A loop that does not exit from its latch
  needs a scalar epilogue [LV-LEGAL] [LV-SRC].
- Recurrences it understands: integer, pointer and FP inductions; reductions
  (add, mul, and/or/xor, min/max, FP add and mul, `fmuladd`, any-of, find-IV,
  find-last); fixed-order recurrences whose users the previous value
  dominates [IVD].
- Strict FP: a reduction needs fast-math on most targets. AArch64 and RISC-V
  can emit *ordered* reductions that keep the exact result, usually slower
  [LV-DOC] [A64TTI]. Only an FP add or `fmuladd` whose accumulator is the phi
  counts as ordered [IVD].
- If-conversion: a load becomes unconditional only if it is dereferenceable
  and aligned across the whole loop. A store is never made unconditional
  ("concurrency restrictions"); every predicated store is masked, and on
  AArch64 masked load/store is legal only with SVE [LV-LEGAL] [A64TTI].
- Memory: at most 8 runtime pointer comparisons by default [LAA]. The
  remainder runs scalar; a vectorized epilogue and interleaving (unrolling by
  register pressure and code size) exist [LV-DOC].
- It runs late: after loop rotation, deletion and distribution, and before
  load elimination, instcombine, SLP and unrolling [LV-PIPE].

### 3.2 GCC

- `vect_analyze_loop` wants a preheader, a single entry, a countable exit and
  an empty latch [GCC-LOOP].
- Since GCC 12, `-O2` vectorizes with the *very-cheap* cost model, which
  "disables vectorization of loops when any runtime check for data dependence
  or alignment is required" [GCC-12] [GCC-OPT].
- Strict FP: FP reductions become fold-left (in-order) reductions; without a
  target instruction that costs an extract and a scalar add per lane
  [GCC-LOOP].
- Transformations that introduce a store are behind
  `-fallow-store-data-races`, on only at `-Ofast` [GCC-OPT].

### 3.3 Dependence testing as LLVM does it

LoopAccessAnalysis reduces every pair of accesses to one byte distance `Dist`
between source and sink [LAA]:

- `|Dist|` beyond the loop's whole range: no dependence.
- `Dist == 0`, same size: safe.
- `Dist < 0` (the later iteration reads what an earlier one has not yet
  written): forward, safe; a store followed by a load may still be flagged
  because the load cannot be forwarded from the store buffer.
- `Dist > 0` (backward): unsafe when `Dist` is under
  `MaxStride * (MinNumIter - 1) + TypeByteSize`; otherwise vectorizable with
  the vector factor capped at `Dist / MaxStride`.

For unit stride that is: a backward dependence of d elements permits a vector
factor up to d. LLVM's general Goff-Kennedy-Tseng dependence analysis exists
but is "(incomplete)", and that the vectorizer does not use it is
*(unverified)* [LL-DA].

### 3.4 Smaller compilers

- HotSpot C2 SuperWord: Larsen-Amarasinghe SLP applied to a loop body after
  unrolling. Adjacent, isomorphic, independent memory operations are the
  seeds, extended along use-def chains. No control flow, no gathers, no
  strided access [C2]. Float and double add reductions keep strict order and
  were measured at about scalar speed; NEON does not vectorize them [C2-RED].
- Go has none upstream; a May 2026 RFC prototypes basic-block SLP on SSA,
  128-bit, no reductions, no cost model [GO-SLP]. V8 has none beyond a Wasm
  128-to-256 revectorization *(secondary)* [V8-NV]. Graal's vectorizer is in
  Oracle GraalVM only [GRAAL].
- That Cranelift, QBE, TCC and MIR have no loop vectorizer is *(unverified)*,
  and no publication was found that measures what share of the benefit a
  minimal vectorizer captures *(unverified)*.

### 3.5 TSVC

- The suite is Callahan, Dongarra and Levine's (Supercomputing '88, Fortran);
  the C version with 151 loops is Maleki et al., PACT 2011, who report 45 to
  71 % vectorized across GCC 4.7, ICC 12 and XLC 11 [MALEKI]. Per-compiler
  counts were seen only second-hand *(unverified)*.
- 2025, `-O3`, no fast-math [SAKIB]:

  | Target | GCC 14 | Vendor compiler | Clang 18 |
  |---|---|---|---|
  | x86 (AVX-512) | 54 % | 50 % (ICX) | 46 % |
  | Arm (A64FX, SVE) | 56 % | 54 % (ACFL) | 47 % |

  All three vectorized the same 35 % (x86) and 45 % (Arm); 36 % and 40 % were
  vectorized by none. About 20 % of the vectorized loops showed no speedup,
  and two of eight indirect-access patterns were vectorized by anyone. (Read
  from the abstract and a summary in the session of 2026-11-11, not the full
  paper.)
- What stays scalar everywhere: first- and second-order recurrences,
  induction variables under branches, variable bounds or steps, loops that
  need fission, interchange, node splitting or array expansion, search loops,
  `x[y[i]]`, reductions through calls [RUSC].
- Where Clang trails GCC on TSVC_2 (AArch64): interchange (s231), outer-loop
  (s2275, s235), control flow (s1161, s276), peeling (s291), predictive
  commoning (s242); LLVM's interchange, distribution, fusion and
  unroll-and-jam are off by default [MEIJER].
- TSVC_2's check is a plain sum of the result arrays, printed; there is no
  tolerance in the sources [TSVC2]. LLVM's test-suite copy sets
  `FP_ABSTOLERANCE 0.0` [LL-TS].

### 3.6 Measured here

TSVC_2 with `iterations` 100, M1 Pro, all 151 checksums compared with
clang `-O3`:

| Build | Run time | Checksums |
|---|---|---|
| clang 23.1.4 `-O3` | 0.46 s | reference |
| clang `-O0` | 1.84 s | 151/151 |
| vcc `-O2` | 2.48 s | 151/151 |
| vcc `-O2`, `VCC_VIR=bl` | 2.46 s | 151/151 |

- clang vectorizes 77 loops, in 66 of the 151 functions (44 %), 55 of them at
  width 4 interleaved 4; with `-ffast-math` 92 loops.
- Its reasons for the rest: no reason given (54), a value used outside the
  loop that is not a reduction (20), unsafe dependent memory operations (19),
  a call (10), an instruction that cannot be vectorized (8), FP reordering
  (7).
- vcc at `-O2` is slower than clang at `-O0` on this suite. The inner loop of
  `a[i] = b[i] + 1` through VIR is 17 instructions: the bound, both array
  addresses (through the GOT) and the constant 1.0 are rebuilt every
  iteration, and each float goes through a general register on its way to
  and from memory.
- VIR built 7 of the 158 functions of `tsvc.c`; the other 151 were refused
  with "a value of one type read as another". Cause: a float argument's
  8-byte slot word was stored as an `f64` (`vw_store`). Fixed the same day:
  158 built and lowered, 151/151 checksums, 1.88 s. (At `iterations` 100 the
  outer loops of s3110 and s13110 run zero times and return an uninitialized
  value; `bench/vcc-tsvc.py` uses 256.)
- clang's `tsvc.s` has 98 `fmadd`/`fmsub`; vcc's has none. The checksums do
  not show it at 100 iterations.

## 4. What makes a vectorizer miscompile

4.1 **Reads past the end.** A load the scalar loop would not have executed
may fault. LLVM hoists a conditional load only when it is dereferenceable and
aligned for the whole loop, and refuses an early-exit loop that "may fault"
[LV-LEGAL]. *Rule: the vector body executes only loads and stores the scalar
loop executes in the same iterations; whole iterations only, the rest stays
scalar.*

4.2 **Stores the program did not make.** Turning `if (c) a[i] = x` into load,
select, store writes memory another thread may own and may write read-only
memory. LLVM never does it; GCC only under `-fallow-store-data-races`
[LV-LEGAL] [GCC-OPT]. NEON has no masked store. *Rule: a conditional store is
not vectorized.*

4.3 **Wrapping induction variables.** The trip count and every address are
computed as if the counter did not wrap. LLVM proves it or adds a runtime
predicate [IVD]. *Rule: the induction's arithmetic carries `VIF_NSW`, or its
type is `i64`/`ptr`, or the loop is not vectorized. The builder sets no
`VIF_NSW` today, and the kernel's `-fno-strict-overflow` must clear it.*

4.4 **Min and max.** `a < b ? a : b` is not IEEE `min`: with a NaN it
returns `b`. LLVM accepts a select-of-compare reduction only with `nnan` and
`nsz` [IVD]. Per lane the idiom is exactly `pmin` (1.2), so a *lane-wise*
select is safe; a min/max *reduction* reorders comparisons and is not.

4.5 **FP reassociation.** A float sum in vector lanes adds in another order.
The alternatives are an ordered reduction, which is as slow as scalar on NEON
[C2-RED], or leaving the loop alone. *Rule: no FP reductions without
`-ffast-math`; integer add, and, or, xor reductions are exact.*

4.6 **FP contraction.** If the scalar remainder fuses `a*b+c` and the vector
body does not, an element's value depends on the trip count. LLVM carries
`fmuladd` as its own recurrence kind [IVD]. *Rule: the vector body contracts
exactly where the scalar code does. vcc contracts nowhere today (3.6), so
both sides agree; the day scalar code gets `fmadd`, vectors get `fmla` in the
same change.*

4.7 **Alignment.** A `float *` is aligned to 4, not 16. *Rule: vector loads
and stores carry the element's alignment; NEON `ldr q` and SSE `movups` need
no more. No peeling.*

4.8 **Dependences.** Section 3.3. *Rule: 6.3.*

## 5. Proposal: vectors in VIR

5.1 **Types.** Six lane-typed 128-bit types after `multi`: `i8x16`, `i16x8`,
`i32x4`, `i64x2`, `f32x4`, `f64x2`. `vir_tysize` is 16 for each. They are
values like any other: block parameters, `select` arms, `load`/`store`,
arguments. No 64-bit or 256-bit vectors in 1.0; AVX and SVE are a later
question (a type byte has room, the allocator's slot sizes do not care).
The verifier's type masks are 16 bits and 13 types fit.

5.2 **Operations.**
- Reused as they are, by letting `OK_T` rows accept vector types: `add sub
  mul and or xor not neg`, `fadd fsub fmul fdiv fneg fabs fsqrt`, `select`
  (a scalar condition picks a whole vector), `bparam`. No integer division,
  and no `mul` on `i64x2` (NEON has none): the opcode table's type mask says
  so per row.
- `load` and `store`: the width is the type's; `VIF_W` has two bits and stays
  the element's log2 size, which is also the alignment's floor.
- New, about a dozen rows: `vsplat` (scalar to all lanes), `vseries`
  (lanes 0, 1, 2, ...: the induction constant, GCC's `vec_series`),
  `vextract` and `vinsert` (lane an immediate), `vicmp` and `vfcmp` (the
  comparison codes of `icmp`/`fcmp`, the result the integer vector of the
  same lane width, lanes 0 or -1), `vbitsel` (per bit), `vshuffle` (two
  operands, 16 validated lane immediates in `extra`), `vshl vlshr vashr`
  (one scalar count), `vs2f vf2s` and the lane-width conversions, `vreduce`
  (add, and, or, xor to a scalar), `vbitcast`.
- Constants: `vsplat` of a scalar constant and `vseries` cover the
  vectorizer. A general `vconst` (16 bytes in `extra`) comes with the GNU
  vector extension, lowered to a literal-pool load.

5.3 **No masks as a type, no undef lanes.** A mask is an integer vector whose
lanes are 0 or -1; nothing checks that, `vbitsel` is defined for every bit
pattern (R1).

5.4 **Allocation.** `vir_rclass` returns the float class for vectors. Spill
slots take their size from `vir_tysize` (4, 8, 16) and are shared only among
equal sizes. Edge moves and exchanges already use whole-register forms
(`eor v.8b` today; it becomes `.16b`), register moves become `mov v.16b`.
Every v register stays caller-saved.

5.5 **Lowering.** arm64: one encoded word per operation, as the scalar FP
operations are today (`I_WORD`); a load or store takes its address in a
general register. A 16-byte spill needs a record the frame layout can patch,
not a pre-encoded word: `I_LD`/`I_ST` grow a `sz` 4 form for q registers.
x86-64 (SSE2) and wasm (simd128) have every operation of 5.2 except
`i64x2` compares on SSE2, which the table's masks exclude there.

5.6 **Three users, one representation.** The GNU vector extension and NEON
intrinsics, an SLP
pass, and the loop vectorizer all produce the same types and operations. The
session of 2026-11-11 recommended that order; the representation is first in
any order.

## 6. Proposal: the loop vectorizer

6.1 **Where.** On the builder's pre-IR in `emit/vir/` (linked instruction
lists, parameters with one operand per predecessor), after `vw_ifconv` and
before `vw_split`: blocks and instructions can still be inserted there, and
`vir_canon` has not fixed the layout. One walk over the innermost loops, no
fixpoint. Off under `-mgeneral-regs-only` (the kernel), `-O1` and below.

6.2 **Shape accepted first.** An innermost loop whose header has two
predecessors, tests `iv < n` (or `<=`, `>`, `>=`) against an invariant, and
whose body is a chain of blocks with one way in and out: straight-line code,
plus the `select`s if-conversion made. Every header parameter is a linear
induction (integer or pointer, constant step) or an integer reduction. No
calls, no volatile, no atomics. Nothing but header parameters can be live
after the loop (the exit is in the header), so the scalar loop that follows
computes every live-out.

6.3 **Memory.** Each address must be `base + s*iv + k` with `base` a symbol
or an invariant value: `s` is the element size (consecutive), its negation
(reversed with a shuffle), or 0 (an invariant load, hoisted when no store in
the loop can alias it). Two accesses with different symbols as base are
independent. Two with the same base and stride, at a distance of d
iterations from the store to the other access:
- d = 0: safe;
- |d| >= 4: safe;
- d < 0 and the load comes first in the body: safe (it reads elements no
  earlier lane has written);
- two stores: safe when the one of the later iteration also comes later in
  the body;
- anything else, a base that is neither, or a stride that is neither:
  not vectorized.
This is LAA's rule (3.3) at a fixed factor of 4. Pointer bases that may
alias get a runtime overlap check in a second step, at most 8 comparisons as
LLVM, none at all under a cost model like GCC's very-cheap.

6.4 **Code.** Before the loop: the trip count in 64 bits; under 4, branch to
the scalar loop. A vector body with pointer inductions for the arrays and a
down-counter, then the scalar loop entered with every induction advanced by
the iterations done and every reduction's partial result. The scalar loop is
the remainder and is not changed. No interleaving and no vectorized epilogue
at first; interleaving by 2 or 4 is what clang does on this machine (3.6)
and is a copy of the body, added once the single copy is right.

6.5 **What this reaches on TSVC.** By the reasons in 3.6, the first shape
covers the loops clang vectorizes minus those needing interleaved access,
gathers or runtime checks: an estimate of 40 to 50 of the 151 functions, to
be replaced by the measured number. The gate is not that number: it is
151/151 checksums with the vectorizer on, and each vectorized loop no slower
than scalar.

6.6 **Before any of it.** In order:
1. The builder accepts TSVC's functions (the refusal in 3.6). Done.
2. `VIF_NSW` from the walker's signedness, cleared by `-fwrapv`.
3. Scalar loops through VIR at least as fast as the direct emitter:
   invariants out of the loop and floats loaded straight into v registers.
   Otherwise the remainder loop and every unvectorized loop get slower when
   VIR takes the function.
4. Types, table rows, verifier, parser, printer, allocator slot sizes (5.1
   to 5.4), with `tests/vir/` units; then lowering (5.5); then 6.2 to 6.4.

## 7. What this changes in the plans

- `SPEC-vir.md` 1.3 drops "vector types" from the exclusions; sections 4, 7
  and 15.2 gain the types, the rows and the 16-byte slot. 1.4's "loop
  transformations" non-goal gets one exception.
- `PLAN-vir.md` gets a step after V4 for 6.6. Its "Inget LICM i 1.0" is in
  the way of 6.6 item 3 and needs a decision.
- A harness, `bench/vcc-tsvc.py`: TSVC_2 at a reduced `iterations`, checksums
  against clang, times per loop (exists); the list of vectorized loops comes
  with the vectorizer.

## Sources

- [AAPCS] https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst
- [APPLE] https://developer.apple.com/documentation/xcode/writing-arm64-code-for-apple-platforms
- [A64TTI] https://github.com/llvm/llvm-project/blob/main/llvm/lib/Target/AArch64/AArch64TargetTransformInfo.h
- [B3-C] https://lists.webkit.org/pipermail/webkit-changes/2022-December/280079.html
- [B3-I] https://raw.githubusercontent.com/WebKit/WebKit/main/Source/JavaScriptCore/jit/SIMDInfo.h
- [B3-T] https://raw.githubusercontent.com/WebKit/WebKit/main/Source/JavaScriptCore/b3/B3Type.h
- [B3-V] https://raw.githubusercontent.com/WebKit/WebKit/main/Source/JavaScriptCore/b3/B3SIMDValue.h
- [C2] https://eme64.github.io/blog/2023/02/23/SuperWord-Introduction.html
- [C2-RED] https://mail.openjdk.org/pipermail/hotspot-compiler-dev/2024-September/080075.html
- [CL-3205] https://github.com/bytecodealliance/wasmtime/issues/3205
- [CL-5031] https://github.com/bytecodealliance/wasmtime/pull/5031
- [CL-ABI] https://raw.githubusercontent.com/bytecodealliance/wasmtime/main/cranelift/codegen/src/isa/aarch64/abi.rs
- [CL-INS] https://raw.githubusercontent.com/bytecodealliance/wasmtime/main/cranelift/codegen/meta/src/shared/instructions.rs
- [CL-IR] https://raw.githubusercontent.com/bytecodealliance/wasmtime/main/cranelift/docs/ir.md
- [GCC-12] https://gcc.gnu.org/gcc-12/changes.html
- [GCC-LOOP] https://github.com/gcc-mirror/gcc/blob/master/gcc/tree-vect-loop.cc
- [GCC-OPT] https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html
- [GCC-R] https://gcc.gnu.org/onlinedocs/gccint/Vector-Operations.html
- [GCC-V] https://gcc.gnu.org/onlinedocs/gccint/Vectors.html
- [GO-B] https://go.dev/blog/archsimd
- [GO-SLP] https://groups.google.com/g/golang-dev/c/4YKD-sMVQUU
- [GO-T] https://raw.githubusercontent.com/golang/go/master/src/cmd/compile/internal/types/type.go
- [GRAAL] https://graalvm.org/dev/reference-manual/java/options
- [IVD] https://github.com/llvm/llvm-project/blob/main/llvm/lib/Analysis/IVDescriptors.cpp
- [LAA] https://github.com/llvm/llvm-project/blob/main/llvm/lib/Analysis/LoopAccessAnalysis.cpp
- [LL-CG] https://llvm.org/docs/CodeGenerator.html
- [LL-D148637] https://reviews.llvm.org/D148637
- [LL-DA] https://github.com/llvm/llvm-project/blob/main/llvm/lib/Analysis/DependenceAnalysis.cpp
- [LL-REF] https://llvm.org/docs/LangRef.html
- [LL-RI] https://raw.githubusercontent.com/llvm/llvm-project/main/llvm/lib/Target/AArch64/AArch64RegisterInfo.td
- [LL-TL] https://raw.githubusercontent.com/llvm/llvm-project/main/llvm/include/llvm/CodeGen/TargetLowering.h
- [LL-TS] https://github.com/llvm/llvm-test-suite/blob/main/MultiSource/Benchmarks/TSVC/CMakeLists.txt
- [LLDB] https://lldb.llvm.org/cpp_reference/ABIMacOSX__arm64_8cpp_source.html
- [LV-DOC] https://llvm.org/docs/Vectorizers.html
- [LV-LEGAL] https://github.com/llvm/llvm-project/blob/main/llvm/lib/Transforms/Vectorize/LoopVectorizationLegality.cpp
- [LV-PIPE] https://github.com/llvm/llvm-project/blob/main/llvm/lib/Passes/PassBuilderPipelines.cpp
- [LV-SRC] https://github.com/llvm/llvm-project/blob/main/llvm/lib/Transforms/Vectorize/LoopVectorize.cpp
- [MALEKI] https://www.microsoft.com/en-us/research/publication/an-evaluation-of-vectorizing-compilers
- [MEIJER] https://llvm.org/devmtg/2024-10/slides/techtalk/Meijer-Loop-Vectorisation.pdf
- [MIR] https://raw.githubusercontent.com/vnmakarov/mir/master/MIR.md
- [QBE] https://c9x.me/compile/doc/il.html
- [RA2] https://raw.githubusercontent.com/bytecodealliance/regalloc2/main/src/lib.rs
- [RUSC] https://2017.russianscdays.org/files/pdf17/55.pdf
- [SAKIB] https://arxiv.org/abs/2502.11906
- [SYSV] https://gitlab.com/x86-psABIs/x86-64-ABI/-/raw/master/x86-64-ABI/low-level-sys-info.tex
- [TSVC2] https://github.com/UoB-HPC/TSVC_2
- [V8-NV] https://groups.google.com/g/v8-users/c/lRBUTOsGhdg/m/yLB1c1SYGwAJ
- [VPLAN] https://llvm.org/docs/VectorizationPlan.html
- [W-22] https://github.com/WebAssembly/simd/pull/22
- [W-SIMD] https://raw.githubusercontent.com/WebAssembly/simd/main/proposals/simd/SIMD.md
