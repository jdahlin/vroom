# Bit-cast extension contracts

The ordinary `tests/vcc/bit_cast.c` regression uses Clang as the runtime
reference. `bitfield-value.c` deliberately has a different contract: a direct
bit-field operand is extracted as a value of its declared scalar type before
reinterpretation. For `unsigned a:3,b:5` initialized to `{5,11}`, casting `b`
therefore yields 11 on every target. Clang23 instead copies containing storage
and yields 93. Copying `sizeof(unsigned)` starting at a packed one-byte object
would read beyond it, so vcc consistently uses scalar extraction, including
constant evaluation. Cast an entire record to inspect its storage; explicitly
cast a field to `unsigned` for portable value semantics across these compilers.

Compile `bitfield-value.c` with vpp `-S` for x86-64 and AArch64 Linux, assemble
and link using the target Clang driver, and execute (qemu for AArch64). It must
return zero. The same fixture must return zero in wasm32/wasm64 builds. This is
a vcc contract test; Clang's executable is intentionally not an equality oracle.
The constexpr counterpart is asserted in `tests/diag/bit_cast_constraints.c`.

`packed-lane-value.c` applies the same declared-value policy to a packed boolean
vector element. A variable index must load its selected bit, return byte 0 or
1, and evaluate the index once. Clang23 copies containing storage for this
operand as well; execute the vcc fixture directly rather than comparing its
result against Clang's executable.
