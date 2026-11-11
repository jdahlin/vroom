# X9 metadata and reviewed lowering maps

`builtins.json` is imported from LLVM commit
`c47d7823685a0e7048795e0cd2d58ad35e454ee9` (the installed Clang 23.1.6 source).
It contains 2029 concrete `__builtin_ia32_*` records expanded with LLVM TableGen,
including every one of the 2002 names found in that installation's resource
headers (117 relevant headers, each SHA256-pinned). The previous vcc builtin table contains 22 of those header names.
This is an inventory of names, **not a claim of implemented code generation**.

The metadata retains exact LLVM prototypes, feature expressions, attributes,
header references, source hashes and licenses. There are 110 distinct feature
expressions; commas and `|` must retain LLVM's conjunction/alternative semantics.
65 records have more than one constant operand. All constant operands are marked
in the signature. 324 records have ordinary literal interval constraints;
323 name specialized rounding/SAE, scale, or tile validators. Specialized
validators are not equivalent to one interval and those records remain
unsupported by the initial ordinary lowering map.

`lowerings.json` explicitly maps 119 reviewed scalar/SSE/AVX512 operations to generic
native instruction classes (108 ordinary/shift and 11 specialized PG rows). Each mnemonic was checked using a Clang wrapper
with the exact argument types and feature flags. Three additional candidates
whose probes optimized to a different instruction were left unsupported.
The reproducible oracle passes 264 positive/constant/feature checks for these
119 mappings; elementwise probes pass 195 native/reduction-IR checks.
Instruction appearance alone does not establish vcc correctness: compiler
runtime, encoding, immediate, feature and ABI tests must validate the consumer.

`elementwise.json` contains 162 integer overloads: 66 min/max, 30 saturating
add/subtract, 33 popcount, and 33 reductions. All 129 non-reduction overloads
were checked against Clang's emitted instruction. The 33 reductions represent
instruction sequences, not a fictional single horizontal-add instruction.
Widths are 16, 32 and 64 bytes; plain char and signed char remain distinct C
types. Floating min/max are intentionally absent: their NaN/signed-zero
contract cannot be implemented by blindly selecting minps/maxps.

## Regeneration

Normal generation and freshness checks are offline and need only Python:

```
python3 bench/gen-x86-intrinsics.py
python3 bench/gen-x86-intrinsics.py --check
python3 bench/test-x86-intrinsic-data.py
# Optional development oracle: generated positive/ICE/feature-negative cases.
python3 bench/probe-x86-intrinsic-data.py --clang clang --output work/x9-probes
python3 bench/probe-x86-elementwise-data.py --clang clang --output work/x9-elementwise
```

To reacquire upstream data explicitly, use a private directory. Every downloaded
file is pinned and checked against its recorded SHA256. `--fetch` only downloads
missing files; it does not silently replace changed files.

```
python3 bench/import-x86-intrinsics.py \
  --source-dir work/x9-upstream --fetch \
  --headers /usr/lib/llvm-23/lib/clang/23/include \
  --llvm-bin /usr/lib/llvm-23/bin
python3 bench/gen-x86-intrinsics.py
```

The importer invokes `llvm-tblgen --dump-json` for `BuiltinsX86.td` and
`BuiltinsX86_64.td`, preserving foreach-expanded records rather than attempting
to infer signatures from builtin names. Header lists are sorted. LLVM tools,
headers and network access are not needed by the compiler or ordinary build.

## Descriptor contract

`intrinsics.inc` uses
`X86_INTRINSIC(ID, NAME, SIG, FEATURES, CLASS, MNEMONIC, ORDER, WIDTH, CONSTRAINTS)`.
Rows are sorted by full builtin name; changing only lowering metadata does not
change an existing row index. Consumers must not persist row indexes across
metadata-version changes.

`SIG` has a result token followed by argument tokens. Scalars are `v` void,
`b` bool, `c` plain char, `a` signed char, `C` unsigned char, `s/S` short,
`i/I` int, `l/L` long, `q/Q` long long, `f` float, `d` double, `h` Float16,
`j` BF16, `z` size_t and `t` uint64_t. `V<count>` prefixes a vector scalar;
`P` and `K` prefix pointer and const. `!` marks every constant-required argument.
LLVM's `_Vector<64, char const *>` spelling denotes a pointer to a const vector,
so it becomes `PKV64c`, not a vector of pointers. Typedef identities for `z/t`
follow the selected target ABI. `WIDTH` is the largest nonpointer vector width
in bytes; metadata includes unsupported 1024-byte AMX vectors, so it needs at
least 16 bits. Constraints are zero-based `arg:min:max` intervals separated by
semicolons; empty does not override constant requirements or special validators.

`ORDER` uses argument digits, `r` for result and `i` for an immediate, in AT&T
order. `XI_TIED` copies argument zero into the result before emission.
`XI_SHIFT` handles scalar runtime count conversion as well as an immediate;
packed shift builtins accept nonconstant and large/negative counts.
Unmapped records have `XI_UNSUPPORTED`, no mnemonic and no operand order.

`elementwise.inc` uses
`X86_ELEMENTWISE(OP, SIG, FEATURES, CLASS, MNEMONIC, ORDER, WIDTH)`.
Select these overloads by exact type, including element signedness and width;
there is no frontend expansion into scalar lanes.

## Exceptions needed by PostgreSQL

* `pmovmskb128` is an ordinary direct SSE2 instruction; CRC32 scalar variants
  use the independent `crc32` feature and scalar tied operands.
* `xgetbv` has implicit ECX input and EDX:EAX output, requiring custom lowering.
* `vec_ext_v2di` is available with SSE2; mapping it to pextrq would incorrectly
  introduce an SSE4.1 requirement.
* Masked loads have real memory and fault-suppression semantics. Merge masks,
  zero masks, destructive ternary logic and extract/insert operations need
  dedicated classes and explicit operand policies.
* String comparisons return implicit ECX/XMM0 or condition flags. Fences,
  MXCSR operations, atomics, gathers/scatters, AMX, MMX state, and privileged
  instructions must retain their side effects; name recognition is insufficient.

The upstream data is Apache-2.0 WITH LLVM-exception; see `LICENSE.TXT`.
