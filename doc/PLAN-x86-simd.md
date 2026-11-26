# x86 SIMD: native instructions for the intrinsic headers

Goal: clang's x86 intrinsic headers compile and run as real vector
instructions, for whatever `-march`, `-m` and `target("...")` enable (SSE
through AVX-512).

## Baseline (2026-11-26, x86-64 host with AVX-512)

Quick check: `bench/stockfish/run.py --arch sse2` (depth 6, ~5 s).
Stockfish 17.1, `bench 16 1 13 default depth`, -O2, 2030154 nodes everywhere:

| build | nodes/second |
| --- | --- |
| clang, SSE2 | 829650 |
| clang, AVX2 | 1706011 |
| vcc, SSE2 | 7284 |

- Vector operations are lane by lane through memory on x86-64
  (`codegen/walk/expression.cpp`), also at -O2 and -O3: `_mm_add_epi16` in a
  function is about 90 lines of assembly and no `paddw`.
- `-mavx2`: `<immintrin.h>` brings in `avx512fp16intrin.h`, which the C++
  front end rejects ([expr.add], [expr.mul] on `_Float16` vectors), so
  Stockfish's AVX2 build fails in `evaluate.cpp` and `nnue/network.cpp`.
- 256- and 512-bit vectors cannot be passed or returned (PLAN-x86.md,
  2026-11-11).
- clang's headers name 2002 `__builtin_ia32_*` functions; the front end
  names 48 of them.
- `-march` and the feature model are done (`driver/target-x64.cpp`).

## Steps

- **XS0** SSE2 builtins Stockfish needs, as vector operations.  Done:
  `--arch sse2` builds and searches clang's node count.
- **XS1** 128-bit vector operations as SSE2 instructions, in VIR's x86-64
  lowering (-O2, -O3) with vector registers, spills and calls, and in the
  direct emitter (-O0, -O1).  Gate: Stockfish SSE2 nodes/second.
  - XS1a (done): the walker's vector operators use SSE2
    (`arch/x64/codegen/simd.cpp`); a function with one leaves VIR for the
    direct emitter.  Stockfish bench depth 13: 8275 nodes/second.  77% of the
    time is in `affine_transform_non_ssse3`; its unpacks are shuffles, which
    are still lane by lane.
  - XS1b (done): shuffles as SSE2 instructions (unpacks, dword and word
    shuffles, rotations, even lanes) and the 32-bit lane multiply.  No
    speedup on Stockfish (8240 nodes/second at depth 13): of the 904
    instructions of `affine_transform_non_ssse3`, about 700 are `mov` and
    `lea`, the vectors copied between frame objects around the inlined
    intrinsics.  Only vectors as values in registers (XS1c) removes them.
  - XS1c: VIR: `i8x16` and `i16x8`, vector shifts and shuffles, vectors from
    source (values, not addresses), vector arguments and results.
- **XS2** (SSE through SSE4.2, AES, PCLMUL, SHA done) Builtins that are one
  instruction: `frontend/x86-instructions.inc`, generated from clang's
  `BuiltinsX86*.td` (`contrib/clang-builtins`) by
  `tools/gen-builtin-registrations.py`, with each builtin's signature,
  mnemonic, form and feature; `arch/x64/codegen/simd.cpp` emits them, and
  `bench/vcc-x86intrinsics.py` tests every row against clang (147 of 147, C
  and C++, -O0..3).  The table lists at its end what it leaves out (control
  registers, fixed registers, `clflush`, `movnti`).  A function with one of
  them is built by the direct emitter, not VIR.  The earlier hand-written
  vector expansions can move into the table.
- **XS3** 256 bits: AVX/AVX2 with VEX, ymm registers, the System V and
  Windows rules for wide vectors, `vzeroupper`, and the front end's
  `_Float16` vector errors.  Gate: Stockfish AVX2 builds.
- **XS4** The rest of SSE3..SSE4.2, AES, PCLMUL, SHA, BMI, FMA, F16C: rows of
  the XS2 table.
- **XS5** AVX-512: EVEX, zmm and mask registers.

Each step: every intrinsic it adds is run against clang's result on random
and extreme operands.
