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

Organize handwritten behavior by builtin family too. The C frontend has a
`builtins-*.cpp` module prefix: registration, generic call routing, math,
atomics, vectors and x86-specific calls live in distinct files. Continue this
at the code generation layers: shared family implementations under `codegen/`
and target-specific implementations under `arch/<cpu>/codegen/`, with focused
files such as `builtins-x86-avx.cpp` for AVX behavior. A target family should
not be hidden in a generic builtin switch. Keep the call dispatcher as a small
router; family-specific validation, folding and lowering should sit with that
family. New modules enter through the existing unity and file-by-file builds.

## Reading the Clang inputs

Use a vroom-owned offline reader for the pinned `.td` files. The
[TableGen Programmer’s Reference](https://llvm.org/docs/TableGen/ProgRef.html)
provides grammar productions alongside the evaluation rules. Implement
the subset exercised by the imported corpus, with source-located errors
for unsupported syntax or unresolved values.

The lexer, recursive-descent parser and evaluator handle the records used
by all eight imported tables, including template arguments and defaults,
inheritance, scoped `let`, `multiclass`, `defm`, `foreach`, and the
required value operators. The generic extractor handles common, x86,
x86-64 and AArch64 records. NEON and FP16 share an extractor, and SVE
and SME share scalable type and name expansion. The reader treats NEON
header bodies as opaque DAGs. Their presence determines whether a
record emits a compiler builtin.

`make -C src -j 6 clang-builtin-reader-check` checks the sorted spelling
set for each table against a digest captured from the pinned LLVM
revision. The common, x86, x86-64 and AArch64 record-based tables export
prototype templates, type substitutions, feature guards, language and
header fields, attribute names, and library aliases. The ACLE tables
export the original encoded prototypes, result and operand variants,
guards, flags, merge suffixes, and intrinsic names. SVE reinterpret
records include tuple sizes. Decoding the full ACLE type language and
extracting immediate-check and attribute operands remain open.
The inventory tool reads both the vendored files and compatible LLVM
source checkouts with the vroom reader.

The reader is a C++20 self-hosting workload. Its host build uses the C++
standard library. A vpp syntax check with GCC 15's C++ include paths
currently stops in `type_traits` and `shared_ptr_base.h`; building and
running the reader with vpp remains a milestone.

## First registry migration

The first shared table covers 30 fixed-signature integer spellings in
[`integer-builtins.inc`](../src/include/frontend/integer-builtins.inc).
C uses each row's ABI-relative signature and AST ID. C++ uses the same
signature via the C AST builder and the row's named fold operation. Both
frontends exercise the generated independent constant and runtime cases.
The generator also emits 13 target-specific registrations for x86 CRC
and add-carry, AArch64 bit operations, and scalar FP16 add and subtract.
These rows use explicit vroom dispatch IDs and derive spellings and
prototype codes from the pinned inputs.

```sh
make -C src -j 6 builtin-registrations
make -C src -j 6 clang-builtin-reader-check
bench/vcc-builtins.py --check-stubs
```

Generated C stubs cover the inventoried `__builtin_*` spellings on their
table's architecture. They use a sorted suffix pool and packed offsets,
consulted only when ordinary builtin and library lookup fail. A stub
reports that the builtin is unimplemented. The table adds no feature
query claims or per-TU registrations. Normal builds consume the generated
includes directly. Only regeneration and freshness checks run the reader.
The C frontend now folds fixed-width bit reversal and rotation in
constant expressions. The shared walker also lowers the fixed-width
rotations for x86-64. The arity check requires rejection of too-few and
too-many calls for all 30 rows in both languages.

Fixed-signature population counts are the starting case.
Clang describes these with one templated record. In vroom, the C table
records three spellings and signatures, the C++ registry lists all three
again for call checking and folding, and the code generators dispatch
all three IDs. The shared walker already gets the machine operand width
from the converted operand. The registry should preserve that property.
[Clang's population-count definitions](https://github.com/llvm/llvm-project/blob/5b10d2d74929eae78912771222c9855af97496f4/clang/include/clang/Basic/Builtins.td#L738)

| Spelling | Converted argument | Result | Operation |
| --- | --- | --- | --- |
| `__builtin_popcount` | `unsigned int` | `int` | Population count |
| `__builtin_popcountl` | `unsigned long` | `int` | Population count |
| `__builtin_popcountll` | `unsigned long long` | `int` | Population count |

Store the public spelling and prototype on the variant. Store the semantic
operation and its fold on the family. Resolve the argument type in each
frontend before folding or constructing the AST. For example, the width of
`unsigned long` is 64 bits on the supported Linux and Darwin targets and
32 bits on MinGW. A test with `1ULL << 40` passed to `__builtin_popcountl`
should fold to 1 on LP64 and 0 on MinGW. Cover the same conversion with a
runtime operand, and check in C and C++ that an argument with side effects
is evaluated once. Exercise direct codegen and VIR with the existing
target test tooling.

Generate adapters for the existing C and C++ lookup entry points and keep
the existing AST IDs for this first migration. This lets the new
definition prove that it can replace duplicated frontend registration
while the current ABI and backend paths remain testable. Check C++ calls
through template instantiation, constant evaluation and runtime lowering,
and compare feature queries before and after the migration. Consolidate
backend dispatch only once the shared description preserves those results.
Use `__builtin_popcountg` as the follow-up that exercises a type-generic
operand without the fixed-prototype conversion.

The upstream inventory and vroom's registry have separate update paths.
Clang's records contain useful spelling, prototype, attribute and feature
information, and some records delegate their type checking to Clang's
handwritten Sema. A TableGen enumerator establishes inventory membership.
Vroom's registry entries must point to vroom's own checking and lowering;
only those entries can contribute implemented support to the coverage
ledger. Retain original Clang table membership so revisions can be
compared even when multiple records generate the same spelling.

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
make -C src -j 6 clang-builtin-reader
bench/vcc-builtins.py --clang-vendored --all-targets --language c --language c++ --json
bench/vcc-builtins.py --llvm-source /path/to/llvm-project --all-targets --language c --language c++ --json
```

With the [vendored inputs](../contrib/clang-builtins/README.md) or a compatible
LLVM checkout, the vroom reader extracts
the common, x86, x86-64, AArch64, NEON, FP16, SVE and SME builtin enumerators.
The JSON preserves membership in each upstream table. The checked-in snapshots
alone contribute 3,178 spellings. At LLVM revision 5b10d2d74929 the union with
those eight tables is 14,690 spellings. Feature-query observations depend on
the selected target and flags. Report status 1 indicates additional vroom
builtin claims relative to the Clang executable used for the probes. Pass
--report-only for inventory exports that should exit successfully on differences.
Probe errors still exit with status 2.

The vendored report counts feature-query outcomes for every spelling in
each table, including names that neither compiler advertises in the
requested profile. For C profiles it also counts calls reported as
undeclared. Neither measure establishes that a valid call lowers correctly.

Run the typed sample probes and the execution tests separately.

```sh
bench/vcc-builtins.py --clang-vendored --sample-calls
bench/vcc-ctests.py -j 6 --suite vcc 'builtin-'
bench/vcc-x86.py -j 6 --suite vcc --obj 'builtin-popcount-abi|builtin-x86-addcarry|x86_crc32|x86_sse_builtins'
bench/cxx-run.py -j 6 gnu-builtin-constexpr-integers
bench/cxx-run.py -j 6 shared-integer-builtins
bench/vcc-builtins.py --check-integer-arity
```

The sample probe builds two typed C calls per upstream table at each
optimization level from `-O0` through `-O3`. NEON, SVE
and SME samples exercise public ACLE entry points. The other samples
call the raw builtin. All 64 builds emit objects with Clang and vroom. The
runtime cases check popcount conversions and evaluation, x86 carry,
CRC and SSE, AArch64 bit operations and FP16 rounding, NEON lane values,
and SVE and SME vector lengths. CI checks the typed samples and
spelling-set parity for all eight upstream tables.

The x86 carry builtin uses unsigned C AST operations, with an explicit
store through the output pointer. The FP16 add and subtract builtins
reuse the half arithmetic path. AArch64 Linux feature-query parity
for `-march=...+fp16` still needs driver support. The SME header currently
offers `svcntsb` and `svcntsd` through `rdsvl`. SME feature tracking,
raw builtins, streaming ABI and ZA state remain open.

Export the per-spelling coverage ledger as JSON Lines.

```sh
bench/vcc-builtins.py --clang-vendored --all-targets --language c --language c++ \
    --coverage-ledger --spelling __builtin_popcountl --spelling __builtin_sme_svcntsb
```

Omit `--spelling` to export the full inventory. Each row preserves source
table membership, available reference records, exact feature-query values
for each selected profile, the optional C undeclared-name probe, and
typed object-emission observations. Header-backed observations identify
the actual entry point and flags. Execution, ABI checks, and invalid-call
evidence still need integration with the ledger before it can enforce the
release gate.

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
  The pinned inputs and their transitive includes are checked in under
  `contrib/clang-builtins` and can be used without an LLVM checkout.
  Vroom's offline reader matches the exact spelling sets for all eight
  tables without LLVM tools. The coverage ledger records query and sample
  object-emission evidence per spelling. All eight tables contribute
  reference records and type-variant metadata.
  GCC common definitions, literal target names, live GCC queries and Clang
  resource header observations are available. Generated GCC target
  registrations, public wrapper classification and the per-stage ledger remain.
- B1 generates 30 fixed-signature integer spellings for the C and C++
  frontends and 13 fixed-signature target registrations for C. Generated
  C stubs provide diagnostics for the remaining raw Clang spellings.
  General prototype decoding and C++ stub lookup remain open.
- B2 and B6 groundwork includes generated scalar regression cases with
  independent expected answers and a CI freshness check. Typed compile
  and runtime samples cover every imported table. B3 includes the FP16
  add and subtract samples. B4 includes the x86-64 add-carry sample.
  B5 includes the SME streaming-length queries. Per-spelling execution
  coverage and the remaining B3–B6 work are pending.

### Consistency check (vcc only)

`bench/vcc-builtins.py --check-consistency --vpp build/linux/src/vpp`
(`bench/vcc-builtins-consistency.py`, no clang at check time, about 1.5 s,
wired into `ci/premerge.sh` as `builtin-consistency`) takes every spelling
of vcc's C table (699 with a typed call; the stub catalog is left out) and
runs vpp for aarch64-linux-gnu and x86_64-linux-gnu, in C and in C++:
`__has_builtin` and `__has_constexpr_builtin`, a typed call under
`-fsyntax-only`, the same call under `-c` at -O0, -O1 and -O2, and a call on
literals as a constant expression (a file-scope array bound in C,
`constexpr` in C++). Calls are built from the table's signature (hand-written
ones for the `.` rows: atomics, overflow, elementwise, classification);
excluded as special forms: `__builtin_counted_by_ref`, `__builtin_os_log_format*`,
`__builtin_matrix_*` (each needs a type or layout no typed call can state),
and the `fprintf`/`fscanf` family whose `FILE *` is not decoded. Known
violations are in `src/test/builtins/consistency-failures.txt` (`--check`:
no new line, no line that passes now; `--bless` rewrites it).

| invariant | known lines |
| --- | --- |
| claimed-not-callable (`__has_builtin` 1, call rejected) | 3 |
| c-only-callable (callable in C, rejected in C++) | 6 |
| not-compiled (callable, `-c` fails at -O0/-O1/-O2) | 249 |
| crash (vpp dies on the call) | 0 |
| constexpr-claimed-not-folded (`__has_constexpr_builtin` 1, no fold) | 140 |
| constexpr-folds-unclaimed (folds, claim 0) | 65 |
| fold-c-only / fold-cxx-only (folds in one language only) | 18 / 75 |

Each step that fixes a violation removes its lines in the same commit.
