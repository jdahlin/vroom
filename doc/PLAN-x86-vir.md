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
- 256- and 512-bit vectors cannot be passed or returned (PLAN-x86.md).
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
  AVX2.  Done for SSE2; AVX2 stops on one TU (`nnue/network.cpp`) at the ABI
  for 256-bit vector arguments and results (XV3), 22 of 23 compile (two C++
  front-end fixes: `array()` mem-initializers, constant array prvalues).
  2026-11-26, -O2, x86-64 host with AVX-512, depth 13, 2030154 nodes:

  | SSE2 | vcc | clang++ |
  | --- | --- | --- |
  | nodes/second | 8502 | 903897 |
  | compile CPU, 23 TUs | 16.4 s | 13.9 s |

  (AVX2: not measured.)
- **XV1** 128-bit vector values from source in VIR, x86-64: `i8x16`, `i16x8`,
  locals, parameters, results, loads and stores of any alignment.  Gate: the
  vector tests at -O2 and -O3 stay in VIR (`bench/vir-failures.py`); arm64
  output unchanged (`bench/vcc-same.py`).
- **XV2** The `x86op` operation, the table and its generator, for the SSE2
  to SSE4.1 builtins Stockfish uses, in VIR and the direct emitter.  Gate:
  Stockfish SSE2 stays in VIR and is faster than XV0's.
- **XV3** 256 bits: types, registers, spills, ABI, `vzeroupper`; the AVX2
  builtins Stockfish uses (add, subtract, shift, pack, multiply-high,
  `maddubs`, `madd`, min, max, compare, `permutevar8x32`, `movemask`,
  extract, loads and stores).  Gate: Stockfish AVX2, node count as clang's,
  nodes/second reported.
- **XV4** The rest of SSE through AVX2, by family.
- **XV5** AVX-512: 512 bits, mask registers, EVEX rounding and broadcast.
- **XV6** Delete the stencils and `tools/gen-x86stencils.py`, or keep them
  for the named few the table cannot express.

## Open

- Does arm64 take the same `target operation` for NEON later?  The plan only
  needs x86-64; the operation should not be named or shaped for x86 alone.
- `-O1`: the direct emitter keeps vectors in frame objects, so it gains the
  instruction but not the registers.
- No size or schedule estimate: XV1 and XV3 are compiler work of a different
  kind from the table, and neither has been prototyped.
