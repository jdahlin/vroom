Runtime wide integer/floating casts
==================================

Linux x86 and ARM support scalar integer casts at the existing `_BitInt`
precisions, without an external helper or a 128-bit truncation of the input.
Integer-to-float conversion uses the full signed magnitude, retaining guard
and sticky bits across all limbs and rounding once to nearest, ties to even.
Float-to-integer conversion decodes the source exponent and significand and
truncates toward zero. Out-of-range, infinity and NaN conversions have no C
value guarantee; the generated code bounds its shifts and returns zero when
the exponent is outside the unsigned magnitude width.

Supported destination/source formats are `_Float16`, `__bf16`, `float`,
`double`, and `long double` (x87 on x86; binary128 on ARM). x86 also supports
`__float128`. ARM retains its explicit `__float128 is not supported on this
target` diagnostic; its binary128 format is `long double`. Other target
backends retain the existing unsupported-runtime-conversion diagnostic.
Integer-to-float conversion constructs nearest-even result bits independently
of the active floating environment. It does not raise hardware inexact or
overflow exceptions. Dynamic rounding modes and floating exception flags are
not implemented by this lowering, including when the driver accepts
`-frounding-math` or `-ffp-model=strict`; see the strict floating-environment
item in TODO.md. Float-to-integer truncation remains toward
zero. The x87 decoder reads its 16-byte storage but uses only the low 64-bit
significand, 15-bit exponent and sign; padding bits 80–127 are masked out.
Constructed x87 results start from initialized 128-bit integer storage.

Run `bench/vcc-bitint-float.py --bin-dir DIR --output DIR [--target
 aarch64-linux-gnu]`. The default matrix covers 129, 193, 310, 500 and 7001
bits, both integer signs, zero, powers, rounding ties, sticky low bits,
normal and subnormal floating inputs, signed extrema, and once-evaluated
operands. Python uses exact integers for IEEE packing and truncation. The
ordinary-C caller communicates only through arrays and scalar parameters;
ARM never asks Clang to implement a wide integer. Every vcc result must match
the exact oracle. Function-body constant casts are checked as well as runtime
casts; `vcc-bitint-constants.py` separately checks static values and constraints.

The x86 harness also records Clang 23 results. Three known oracle disagreements
are reported explicitly in its output and commands.json:

* BF16 double rounding: `2**64 + 2**56 + 1` converts through float and
  `__truncsfbf2`, producing `0x5f80` instead of `0x5f81`.
* x87 double rounding: `2**127 + 2**63 + 1` converts through binary128 and
  `__trunctfxf2`, losing the sticky bit and rounding down instead of up.
* Unsigned binary128 sign: volatile unsigned `_BitInt(129)` `2**128`
  converts to negative `-2**128` (`0xc07f...`) rather than positive (`0x407f...`).

These are not exemptions for vcc. Its complete matrix uses the independent
oracle, and Clang disagreements for the other formats remain fatal.
