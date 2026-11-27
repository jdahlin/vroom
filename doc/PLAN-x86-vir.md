# x86 intrinsics through VIR

Status: plan, nothing implemented.  Follows `PLAN-x86-simd.md`, whose
stencils (XS2b) it replaces.

## Why

Stencils give coverage but nothing can optimize them: a function that uses
one leaves VIR for the direct emitter, operands pass through memory, and
`-S` shows bytes.  The aim is what clang does: intrinsics are values and
operations the optimizer sees.

## What clang does

Measured 2026-11-26, clang 23 `-O1 -emit-llvm`, one version (one value of
the constant operands) of each of the builtins that have stencils:

| lowered to | builtins |
| --- | --- |
| generic IR only (arithmetic, shuffles, selects) | 482 |
| an `llvm.x86.*` target intrinsic | 1274 |
| not measured | 40 |

Of the 1274, at `-O2` and not counting moves: 1187 are one instruction, 32
two, 49 three or more, 6 none.  (Constant loads count as moves, so some of
the "one" may need a constant.)

## What vcc has

- VIR has `f32x4`, `i32x4`, `f64x2`, `i64x2` (SPEC-vir.md 7.5) for the loop
  vectorizer, lowered on arm64 and x86-64.  No `i8x16`, `i16x8`, no 256 or
  512 bits, no mask type, loads and stores of 16 bytes.
- The walker never builds a VIR vector from source: the vector hooks make
  `virbuild` give up on the function.
- 256-bit vectors are passed and returned only by the direct emitter
  (System V); 512-bit ones not at all.
- Instructions: `frontend/x86-instructions.inc` (147 builtins, one SSE
  instruction each) and the stencils.

## Design

1. **Vector values from source.**  A GNU vector local or parameter is an SSA
   value in VIR, not a frame object.  Types by width (v128, v256, v512) and
   lane.  Registers xmm/ymm/zmm, spill slots of the width, `vzeroupper`
   before calls out of 256-bit code, the System V and Windows rules for
   passing them.
2. **Generic operations** for the builtins clang lowers generically: VIR's
   vector operations, extended (shuffle, saturating add and subtract, min,
   max, shifts, conversions, select by mask).  A mask is a lane vector as
   today; AVX-512's `k` masks are a lowering matter (an integer operand,
   expanded or kept in a `k` register).
3. **One target operation** for the rest: `x86op #row, a, b, c, imm`, typed
   by the table, with the row's effects (pure, reads memory, writes memory,
   reads or writes the floating-point environment, other).  Pure ones are
   value-numbered, hoisted and removed like any operation; nothing looks
   inside.
4. **The instruction table**, generated: per row the builtin's signature,
   feature, effects, mnemonic, encoding form (operand order, which operand
   may be memory, the destination tied or not, immediate, mask, rounding)
   and clang's IR intrinsic.  Sources: the `.td` records for signature and
   feature; clang's `-emit-llvm` for generic or target; clang's `-S` of the
   stencil source for the mnemonic and operand order.  A row the generator
   cannot read stays a stencil.
5. **The direct emitter uses the same table** (-O0, -O1, and VIR's fallback
   per function): operands into registers, the instruction, the result out.
6. **The assembler** learns each mnemonic's VEX and EVEX forms.  Checked
   against `llvm-mc` as the existing cases are.

## Steps

Each step keeps `bench/vcc-x86intrinsics.py` green; a builtin's stencil is
deleted in the commit that gives it a table row or a generic operation.

- **XV0** Baseline: add `--arch avx2` to `bench/stockfish/run.py`; get it to
  compile; record nodes/second and compile time against clang for SSE2 and
  AVX2.  Done.  It took two C++ front-end fixes (`array()` mem-initializers,
  constant array prvalues) and, from XV3, the System V rules for 256-bit
  arguments and results in the direct emitter (`src/test/abi/avx_vectors`;
  not VIR, Windows or 512 bits).  2026-11-26, -O2, x86-64 host with AVX-512,
  depth 13, 2030154 nodes everywhere:

  | build | vcc nodes/second | clang++ nodes/second | vcc compile CPU | clang++ |
  | --- | --- | --- | --- | --- |
  | SSE2 | 8502 | 903897 | 16.4 s | 13.9 s |
  | AVX2 | 50140 | 1982572 | 19.1 s | 15.2 s |

- **XV1** 128-bit vector values from source in VIR, x86-64: `i8x16`, `i16x8`,
  locals, parameters, results, loads and stores of any alignment.  Gate: the
  vector tests at -O2 and -O3 stay in VIR (`bench/vir-failures.py`); arm64
  output unchanged (`bench/vcc-same.py`).
  - XV1a (done): VIR builds `+ - * / & | ^` of two vectors of 4 lanes of
    `int` or `float` or 2 of `long long` or `double` as vector load,
    operation and store, so such a function stays in VIR (-O2, -O3).  The
    vectors still live in memory (copies are two 8-byte moves), so no speed
    claim.  Passes that sized an access by its width field now take all 16
    bytes of a vector one (`virbuild_access_bytes`).
  - XV1b (done): a copy of a 16-byte vector is one vector load and store
    of its type, and slot promotion keeps a slot read and written whole as
    one vector type in a register: across loops, branches and calls.  A slot
    reinterpreted as another vector type, zeroed by memset or never stored
    stays in memory.  A loop of lane arithmetic (4 x int, 1024 vectors,
    200000 rounds, x86-64): -O1 (the direct emitter, what -O2 did before)
    1.48 s, -O2 0.16 s, clang -O2 -fno-vectorize 0.15 s.  Stockfish is
    unchanged: its functions use intrinsics and leave VIR (XV2).
  - XV1c: `i8x16` and `i16x8`, vector shifts, comparisons and shuffles;
    vector parameters and results.
- **XV2** The `x86op` operation, the table and its generator, for the SSE2
  to SSE4.1 builtins Stockfish uses, in VIR and the direct emitter.  Gate:
  Stockfish SSE2 stays in VIR and is faster than XV0's.
  - XV2a (done): the `x86` operation (SPEC-vir.md, opcode 94) for the lane
    operators that are one instruction (bytes and words included) and the
    table's builtins of one or two vectors and perhaps a constant; pure, so
    value-numbered.  A function of those and loads and stores has no stack
    traffic.  Stockfish is unchanged: 13 of `nnue/network.cpp`'s 559
    functions still leave VIR for a vector operation, the hot ones among
    them.
  - XV2b (done for what Stockfish SSE2 uses): shuffles (rotations and the
    even lanes as two or three operations), shifts by a count, and a vector
    with a number (the number in every lane).  No function of
    `nnue/network.cpp` leaves VIR for a vector operation.  Stockfish SSE2,
    depth 13, x86-64: 8829 to 118784 nodes/second (clang++: 919870).
    AVX2 is unchanged (50579): its operations are 256 bits (XV3).
  - Left: vector parameters and results, comparisons that need inversion or
    a sign bias, 32-bit multiply on the generic path, the front end's lane
    expansions (`madd`, `mulhi`, `packus`, saturation) as one instruction.
- **XV3** 256 bits: types, registers, spills, ABI, `vzeroupper`; the AVX2
  builtins Stockfish uses (add, subtract, shift, pack, multiply-high,
  `maddubs`, `madd`, min, max, compare, `permutevar8x32`, `movemask`,
  extract, loads and stores).  Gate: Stockfish AVX2, node count as clang's,
  nodes/second reported.
  - XV3b (chosen over real 256-bit types, as a first step): a 32-byte lane
    operator and the AVX2 builtins that work on each half alone are two SSE
    operations; `movmskps256` and `vpermd` are scalar operations; frames with
    objects aligned to 32 or 64 are realigned by the x86-64 lowering.
    Stockfish AVX2, depth 13: 50600 to 91800 nodes/second (clang++ 1990000;
    SSE2 by vcc: 119900).
  - 32-byte vector parameters (done by inlining, not by an ABI): VIR
    inlines a static function that passes or returns one, whatever its
    shape; `extract128i256` is a copy.  No function of `nnue/network.cpp`
    leaves VIR for a vector reason.  Stockfish AVX2, depth 13: 91800 to
    106700 nodes/second (measured with another session compiling; clang++
    1990000).
  - XV3c (done): a real 256-bit value, `i64x4`, under AVX2: copies, loop
    block parameters, spills, `vzeroupper` before calls and returns; lane
    operators of two vectors and the builtins that work on each half are one
    VEX instruction (the assembler gained the forms).  Stockfish AVX2
    (`-O3`, depth 13, node count as clang's): 465 k nodes/second, from about
    121 k (`-O2`).  clang: 1979 k.
  - XV3d (done): `vsplat` of 256 bits from the set intrinsics' element
    stores, `vmovmskps`, and the shift builtins as one VEX shift; BMI2's
    `pext` and `pdep` are VIR's x86 operation on general registers (their
    stencils took whole functions out of VIR).  Stockfish AVX2 `-O3`:
    1239 k nodes/second (`USE_PEXT`).
  - Left: the 32-byte ABI in VIR (calls that are not inlined); shift
    operators, vector-with-number operations and shuffles on 256 bits
    (still two halves); the front end's lane expansions (`madd`, `maddubs`,
    `mulhi`, `packus`, saturation) as instructions.
- **XV4** The rest of SSE through AVX2, by family.
- **XV5** AVX-512: 512 bits, mask registers, EVEX rounding and broadcast.
  - XV5a (done): `i64x8` under AVX-512BW, along XV3c's and XV3d's paths:
    copies, block parameters, spills, splats, lane operators, shifts and the
    builtins that work on each 128-bit part as one EVEX instruction (the
    assembler gained the forms).  Byte and word compares go through `k1`
    (`vpcmpgtw`, `vpmovm2w`), so `min` and `max` become one instruction.
    Stockfish `-O3 -march=native` (depth 13, node count as clang's): 250 k
    nodes/second, from 76 k.  clang: 2157 k.
  - XV5b (done): `vpdpbusd` (`x86dot`) and the unmasked 512-bit dword
    compares greater, equal and less.  Native Stockfish: 1386 k.
  - VEX (done): in a function whose target has AVX the compiler's own SSE
    instructions are written in their VEX forms (`codegen/vex.cpp`), as
    clang does; a loop mixing legacy SSE with `zmm` code ran three times
    slower.  The assembler derives the forms it has no row for from the SSE
    encodings.  Stockfish `-O3`: native 1386 k to 1460 k, AVX2 1239 k to
    1406 k.
  - Load folding (done): a 32- or 64-byte load used once by the next
    operation is its memory operand (`vpdpbusd` too).  Stockfish native
    1560 k to 1594 k.  Next: registers `zmm16`-`zmm31` (VIR has 16; the
    accumulator loops keep 16 vectors live and spill).
  - Left: mask registers as values (the `_mask` intrinsics), the other
    three-operand instructions, the 64-byte ABI in VIR, shift
    operators and shuffles, EVEX rounding and broadcast.  Without VNNI
    Stockfish's `network.cpp` does not compile (a 64-byte argument).
- **XV6** Delete the stencils and `tools/gen-x86stencils.py`, or keep them
  for the named few the table cannot express.

## Open

- Does arm64 take the same `target operation` for NEON later?  The plan only
  needs x86-64; the operation should not be named or shaped for x86 alone.
- `-O1`: the direct emitter keeps vectors in frame objects, so it gains the
  instruction but not the registers.
- No size or schedule estimate: XV1 and XV3 are compiler work of a different
  kind from the table, and neither has been prototyped.
