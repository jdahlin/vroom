# Builtin coverage for C and C++

Implement the builtin and intrinsic families exposed for arm64 Linux and Darwin,
x86-64 Linux and MinGW, in C and C++. Track wasm separately while it is disabled
by default. Inventory pinned Clang and GCC releases, their public headers, and
the platform specifications. Include public aliases, compiler primitives used
by headers, and special source constructs with builtin spelling. Record
applicability for other architectures, operating systems and languages.

## Inventory and compatibility

Key the inventory by spelling, target, language, and applicable CPU extensions,
language mode and ABI. Track provenance, canonical operation and entry point
(source builtin, special syntax, header API, or standard library name).
Preserve separate profiles where Clang and GCC differ. Use Clang behavior as
the default and cover GCC-only operations where applicable. Resolve conflicts
explicitly against the compatibility profiles.

Extract Clang's generated builtin and target tables and ACLE and x86 header
interfaces. Extract GCC's common definitions, target family registrations,
headers and tests. Compare both inventories with compile probes, including
feature and language variants. Check in normalized snapshots and their source
versions. Compiler builds use vroom's generated data.

Measure each stage separately: inventoried, declared, semantically checked,
constant-evaluable where specified, lowered at each optimization level, and
verified by execution or code and ABI checks. Report public spellings and
canonical operations separately, with gaps by target, language and family.
An affirmative feature query is an observation, and additional evidence is
required for every later stage.

[Clang's builtin generator](https://github.com/llvm/llvm-project/blob/main/clang/utils/TableGen/ClangBuiltinsEmitter.cpp) ·
[GCC builtin families](https://gcc.gnu.org/onlinedocs/gcc/Built-in-Functions.html)

## Definitions, AST and lowering

Use one declarative source per operation family. Expand widths, element types,
operand forms, suffixes and feature combinations from family parameters.
Compile readable prototypes to compact type descriptors and resolve
ABI-dependent types in each frontend. Describe aliases, result type relations,
evaluation and memory effects, argument constraints, availability and lowering
strategy. Express reusable constraints such as constant integer, lane index
and immediate range once. Keep custom checks with the family. Reserve special
parsing for forms such as offsetof that take operands beyond expressions.

Generate spelling lookup, operation and variant descriptors, feature-query
data, hook dispatch and public wrappers. Reject overlapping definitions,
unresolved types or hooks, invalid argument indices, inconsistent constraints
and missing target mappings. Check deterministic outputs in CI. Use compact
read-only tables whose size adds no proportional startup work per TU.

Resolve fixed signatures using each frontend's normal call conversions. Share
type relations and argument constraints through the descriptor. Keep C++
template dependence and overload resolution, frontend-native types, and
diagnostic locations with the respective frontend. C++ carries builtin
identity through instantiation and lowering. Use one typed constructor for
the existing codegen AST builtin node and ordinary call nodes for libc
calls. A spelling or width variant needs no manual AST layout, per-frontend
registration or switch arm.

Reuse canonical typed operations across direct codegen and VIR. Describe
instruction mappings by operand form and feature guard. Keep handwritten
lowering for complex sequences and ABI-sensitive operations. Use library
calls when the target provides the required symbol and behavior, and vroom
runtime helpers for compiler-provided operations that require them. Record
assembler, register allocation, and ABI prerequisites per operation.

Generate public inline wrappers where they express the API. Keep compiler
recognition for underlying primitives, immediate checks and special syntax.
Share feature resolution while retaining context-specific rules. Translation
unit feature queries and function target attributes have different contexts.

[Clang declaration synthesis](https://github.com/llvm/llvm-project/blob/main/clang/lib/Sema/SemaDecl.cpp) ·
[Clang call checks](https://github.com/llvm/llvm-project/blob/main/clang/lib/Sema/SemaChecking.cpp) ·
[NEON header and compiler table generation](https://github.com/llvm/llvm-project/blob/main/clang/utils/TableGen/NeonEmitter.cpp)

## Implementation steps

- **B0 Inventory.** Make coverage tooling portable. Add pinned Clang and GCC
  extractors and a checked coverage ledger for compiler primitives, public
  headers, special forms and feature profiles. Seed discovery with the existing
  generated query data.
- **B1 Shared registry and typed construction.** Establish the family schema,
  generate the registry, and route fixed-prototype calls through both
  frontends and a shared typed AST builder. Migrate existing definitions, folds,
  queries and custom checks by family. Retire the old tables after parity.
- **B2 Generic and library families.** Cover bit and integer operations,
  overflow and carries, atomics and legacy sync, memory and fortify,
  object sizes, varargs, stack and control flow, and C/C++ language
  constructs. Verify evaluation order, memory ordering, address-taking,
  disabled builtin flags, constant evaluation and runtime helper ABI.
- **B3 Math and floating point.** Cover classification, comparison, rounding,
  conversions, complex and libm families across half, bfloat16, float,
  double, target long double and binary128 as applicable. Record exact
  folds, instructions, runtime helpers and library symbols. Test NaNs,
  signed zero, infinities, subnormals, rounding modes, FP exceptions,
  errno and math-flag combinations. Validate extended-format ABI and
  symbol names per sysroot.
- **B4 x86-64.** Cover scalar/system and SIMD families through SSE, AVX,
  AVX2 and AVX-512 and inventoried extensions, including masks, memory
  operands, crypto and AMX. Close assembler, register class, vector ABI
  and OS-state dependencies per extension. Generate or reconcile public
  intrinsics headers. Verify Linux and MinGW separately.
- **B5 arm64.** Cover scalar ACLE, system and memory operations, NEON,
  crypto and other fixed-width extensions, then SVE/SVE2 and SME. Implement
  scalable vectors, predicates and tuples, sizeless-type rules, spill sizing,
  register classes, SVE calling conventions, streaming mode and matrix state
  as required. Generate ACLE headers and verify Linux and Darwin separately.
- **B6 Coverage gate.** Require positive semantic and lowering evidence for
  every applicable spelling. Generate rejection cases for arity, types,
  immediate ranges and disabled features. Exercise O0 through O3, compare
  ABI and instruction encodings, and run representative operations on capable
  hardware or QEMU. Record execution gaps separately from assembly evidence.
  Test resource headers alone and in kernel, libc, MinGW, CPython and FFmpeg
  builds. Add the completed matrix to release validation. Require explicit
  dispositions on upstream inventory changes.

B2 through B5 share B1's interfaces and finish against B6. Run bounded
per-family checks in CI and the full matrix in scheduled or release testing.
Track cold kernel compile time, allocation per TU and registry lookup cost
against a pre-migration build.

## Running the inventory

With a built vpp and Clang on PATH, run:

```sh
bench/vcc-builtins.py --all-targets --language c --language c++ --json
bench/vcc-builtins.py --target x86_64-linux-gnu --flag=-mavx2 --language c++
bench/vcc-builtins.py --llvm-source /path/to/llvm-project --all-targets --language c --language c++ --json
```

With an LLVM checkout and a matching clang-tblgen, the source mode extracts
the common, x86, x86-64, AArch64, NEON, FP16, SVE and SME builtin enumerators.
The JSON preserves membership in each upstream table. The checked-in snapshots
alone contribute 3,178 spellings. At LLVM revision 5b10d2d74929 the union with
those eight tables is 14,690 spellings. Feature-query observations depend on
the selected target and flags. Report status 1 indicates additional vroom
builtin claims relative to the Clang executable used for the probes. Pass
--report-only for inventory exports that should exit successfully on differences.
Probe errors still exit with status 2.

[LLVM revision 5b10d2d74929](https://github.com/llvm/llvm-project/tree/5b10d2d74929/clang/include/clang/Basic)

For GCC source provenance and GCC's live feature queries on x86-64 Linux:

```sh
bench/vcc-builtins.py --gcc-source /path/to/gcc --gcc gcc --target x86_64-linux-gnu --language c --language c++ --json
```

The source mode preprocesses GCC's common builtin definitions with GCC's
own family macros. It records expanded names, type keys, and conditions for
registration, library aliases and fallback. For the GCC revision pinned by
the test suite this yields 2,031 records. The target source scan also records
literal x86 and AArch64 names by file. The combined baseline from the
Clang and GCC inputs above contains 18,490 candidate spellings. GCC's
constructed target names and
target availability still need a fuller extraction. Select the GCC
executable whose reported target matches the requested profile.

Use repeatable header arguments to inventory Clang resource headers under
the selected target and language. The report records function declarations
and function-like macros from the header and its includes, including internal
helpers. Keep this evidence separate from primitive feature queries and use
it to build a public wrapper ledger.

```sh
bench/vcc-builtins.py --target x86_64-linux-gnu --header immintrin.h --no-calls
bench/vcc-builtins.py --target aarch64-linux-gnu --header arm_neon.h --header arm_sve.h --header arm_sme.h --no-calls
```

[GCC builtin definitions at the pinned test revision](https://github.com/gcc-mirror/gcc/blob/917da9c6e019407c9d469455ad4c50c062470d6c/gcc/builtins.def)

## Status

- B0 in progress. Portable C/C++ feature-query matrix and C name recognition
  probe implemented. Clang source extraction covers the eight tables above.
  GCC common definitions, literal target names, live GCC queries and Clang
  resource header observations are available. Generated GCC target
  registrations, public wrapper classification and the per-stage ledger remain.
- B1 groundwork includes the existing C AST builder's builtin lookup and
  call construction. The general family registry and migration remain.
- B2 and B6 groundwork includes generated scalar regression cases with
  independent expected answers and a CI freshness check. B3 through B5
  and the remaining coverage gates are pending.
