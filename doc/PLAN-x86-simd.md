# x86 SIMD: native instructions for the intrinsic headers

Goal: clang's x86 intrinsic headers compile and run as real vector
instructions, for whatever `-march`, `-m` and `target("...")` enable (SSE
through AVX-512).

## Baseline (2026-11-26, x86-64 host with AVX-512)

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
- **XS2** Builtins that no vector operation expresses (`pmaddwd`, packs,
  `pmaddubsw`, ...) as target operations: a table of builtin, signature,
  feature and instruction, generated from clang, replacing the hand-written
  expansions.  A builtin used without its feature is an error, as clang's.
- **XS3** 256 bits: AVX/AVX2 with VEX, ymm registers, the System V and
  Windows rules for wide vectors, `vzeroupper`, and the front end's
  `_Float16` vector errors.  Gate: Stockfish AVX2 builds.
- **XS4** The rest of SSE3..SSE4.2, AES, PCLMUL, SHA, BMI, FMA, F16C: rows of
  the XS2 table.
- **XS5** AVX-512: EVEX, zmm and mask registers.

Each step: every intrinsic it adds is run against clang's result on random
and extreme operands.
