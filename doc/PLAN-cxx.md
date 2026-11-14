# C++ front end, first plan: validate the cache structure

**Goal:** find out, with as little C++ as possible, whether vroom's design
(one resident vccd, state shared between TUs, a direct emitter at -O0) wins
for C++ where C++ spends its time: **template instantiation and the code it
produces in every TU**.  `RESEARCH-cplusplus.md` gives the reasons; this plan
turns its sections 3 and 6 into steps with measurable gates.

This is not "vcc compiles C++".  It is the smallest front end that can
compile one small, heavily tested C++ project correctly, plus the two caches
that would make it fast, each with a verification mode.  If the gates in X6
fail, the result is still worth having: we know C++ needs something else
before anyone writes a full front end.

## Proof of concept goal (decided 2026-11-13)

**nlohmann/json's full test suite (all 71 TUs) built by vcc at -O2 through
VIR, every test passing, 30 times faster than clang++ -O2.**  Baseline,
measured on this machine (x86-64 Linux, clang 23.1.6, libc++, `-g0`, compile
only, `-j 6`): clang -O2 takes **243 s CPU, 41.5 s wall**; -O0 127 s CPU,
21.6 s wall.  30x is **8.1 s CPU** for all 71 TUs (1.4 s wall at `-j 6`).
Measured cold (vccd started for the build) and warm (one TU edited, vccd
kept), and reported apart; the goal is the cold number.

What that needs, from X0's numbers: the caches of X5 (json repeats 85 % of
its instantiations), VIR's -O2 on the remaining unique code with a budget of
roughly 4 s of CPU for the build, and an instantiator several times faster
than clang's.  The comparison is honest only if vcc's code is in the same
class as clang's -O2: the gate includes json's tests running within 1.5x of
clang -O2's run time.

Progress is measured by `bench/cxx-json.py`: per TU, how far vcc gets (the
first unsupported construct), then which tests pass, then time.

## Can json reach 30x at vcc -O0 against clang -O2? (estimate, 2026-11-14)

vcc does not generate C++ code yet, so this is an estimate from parts, each
measured on this machine (x86-64 Linux, clang 23, CPU seconds, compile only).

| Measurement | Value |
|---|---|
| clang -O2, the 71 TUs one by one (the baseline) | 243 s; 30x is **8.1 s** |
| clang -O0, the 38 tests that do not reconfigure json, one by one / as one unity TU (`bench/cxx-unity.py`) | 45.0 s / 9.5 s: sharing gains **4.7x** |
| clang -O2, the same | 76.3 s / 26.7 s (2.9x) |
| clang -O0 unity TU's split | front end 6.8 s (77 %, mostly instantiation), back end 2.0 s |
| clang -O0, a TU of the shared headers only (json, doctest, libc++) / libc++ only | 1.10 s / 0.45 s per TU |
| vcc today, the 71 TUs one by one: preprocessing / preprocessing + C++ parse (no AST, no code) | 4.3 s / 8.4 s (60.4 M tokens) |
| tokens: the shared headers / a TU's own (average) | 834 485 / 17 000: shared, 2.0 M instead of 60.4 M |
| vcc against clang -O0 on C (SQLite amalgamation): -fsyntax-only / -c | 0.05 vs 0.22 s (**4.4x**) / 0.26 vs 0.66 s (back end **2.1x**) |
| vcc -O0 against clang -O2 on C (SQLite) | 0.26 vs 9.8 s (38x) |

The model.  With headers, instances and inline functions shared across TUs
(vccd's caches, X5), a build costs what clang's unity TU costs, plus what
cannot be shared.  The 48 TUs that leave json alone: about 1.1 + 8.4 x 48/38
= 11.7 s at clang -O0 speed.  The 23 that reconfigure json (`#define private
public`, `JSON_ASSERT`, `-fno-exceptions`) share only libc++: about 23 x
(1.18 - 0.45) = 16.8 s.  Unique work: about **29 s at clang -O0 speed**.  vcc
must do it in 8.1 s, 3.6x faster than clang -O0.  At vcc's measured C
ratios (front end 4.4x, back end 2.1x, weighted 77/23): 29 x (0.77/4.4 +
0.23/2.1) = **8.2 s, about 29.6x**.

Verdict: **reachable, but only just**, and only if three things hold.
1. vccd shares headers, instances and inline code across TUs (X5): without
   it vcc's preprocessing and parsing alone already take 8.4 s.
2. vcc's C++ front end keeps its C front end's advantage over clang (4.4x)
   on semantics and instantiation.  Not measured yet: the C++ front end has
   no semantic analysis.  This is the main risk.
3. The back end stays at least 2.1x faster than clang -O0's.

The levers, by size:
- **The 23 reconfiguring TUs: 58 % of the unique work.**  Their changes
  leave types and code alone (`private` -> `public` changes access only,
  `JSON_ASSERT` one macro), but they change json.hpp's tokens, so a cache
  keyed on tokens shares nothing of json with them.  Sharing them (a key
  that ignores access specifiers, region-level header caching around the
  changed macros) would bring the unique work to about 13 s at clang speed,
  needing 1.6x: about 60x.
- The back end: vcc -O0 emits 1.6x clang -O0's bytes on SQLite; a faster or
  smaller -O0 path adds margin directly (23 % of the work).
- A warm rebuild after editing one test is far beyond 30x: only that TU's
  own work (a few percent of a TU) is left.

Not counted: driver and process overhead per job (71 jobs), the cache's
lookups and checks, writing the objects (the COMDAT bytes are copies).

The threshold, and the C++ evidence for it.  With the back end at 2.1x,
30x needs the front end at F with 29 x (0.77/F + 0.23/2.1) <= 8.1: **F >=
4.5**.  vcc's C++ front end as it is (parse, name lookup, class templates,
no expression semantics), `-fsyntax-only` CPU against clang's, mean of 3:

| TU | clang | vcc | ratio |
|---|---|---|---|
| the shared headers only (declarations, class templates, inline bodies) | 0.660 s | 0.103 s | 6.4x |
| unit-32bit (clang also instantiates function bodies; vcc not yet) | 0.733 s | 0.103 s | 7.1x (upper bound) |
| unit-algorithms (likewise) | 1.247 s | 0.103 s | 12.1x (upper bound) |

**Determination.**  30x with caches keyed on tokens alone is on the edge:
the evidence brackets the front end between 4.4x (vcc's full C front end,
semantics included) and 6.4x (vcc's C++ front end without expression
semantics), around the 4.5x needed, so 29.6x to about 34x.  It is not a
safe target as it stands.  With the 23 reconfiguring TUs sharing json
(their unique work falls to about 13 s at clang speed), the requirement
falls to 1.6x and the evidence clears it by a wide margin: **30x is
reachable if and only if those TUs share, or the C++ front end with
semantics stays at least 4.5x faster than clang's.**  The semantic
analysis is therefore to be measured against that 4.5x as it is built.

## Targets

Two targets, the first a stepping stone to the second.  Both have test
suites far stricter than ours, so a pass means something.

1. **libc++'s own tests for a header subset**: `<type_traits>`,
   `<utility>`, `<tuple>`, `<optional>`, `<variant>`, `<array>`,
   `<string_view>`.  Small TUs (one feature each), lit-driven, written by the
   library's authors to break compilers, and almost all of it is templates,
   SFINAE, constexpr and overload resolution without much runtime code.  The
   headers are libc++'s, which already assume clang's builtins, the same
   reference vcc copies everywhere else.
2. **nlohmann/json** with its unit tests.  One amalgamated header (~25k
   lines) included by every one of ~100 test TUs, each instantiating
   `basic_json` and its helpers in different ways: an almost perfect
   experiment for a cross-TU instantiation cache.  It is fuzzed (OSS-Fuzz),
   reports full line coverage, and its tests use doctest (one more header,
   also templates).  It needs `<string>`, `<vector>`, `<map>`, iostreams:
   a bigger slice of libc++ than target 1.

Rejected for now: {fmt} (good, but its tests need GoogleTest, which is a
second large C++ project); Abseil, LLVM (far too big); a hand-written suite
(not "very well tested").

Standard library: **libc++ only** (macOS has it; Linux CI gets `libc++-dev`
from apt.llvm.org next to clang 23).  libstdc++ uses GCC builtins vcc would
also need; that is later.  Dialect: `-std=c++20`, no modules, no
coroutines, no exceptions at first (`-fno-exceptions`, which libc++'s test
suite supports), then exceptions in X7.

## Rules

- Clang is the reference for every claim we can check: record layout,
  mangled names, vtables, constexpr results, which overload wins.  As for C,
  a harness asks clang to verify (a `vcc-sema.py layout` for C++).
- Each cache has a verification mode that does the work both ways and
  compares, and the gates require zero mismatches on the targets.
- The C front end does not get slower: `bench/vbench.py gate` and
  `bench/vcc-same.py` on every step that touches shared code.

## Order: hardest first

The steps are numbered by dependency, but the work goes to the hard
problems first: X0 (measured), then X1 and X2 only as far as templates need
them, then X4 and X5 straight away.  X3 is not done as a step of its own:
each of its features comes in when a libc++ test that X4 targets needs it.
The questions that decide the plan are the instantiation key and order
independence (X0's findings), and they get answered before broad language
coverage, not after.

## Steps

### X0 Measure first, with clang only (no vcc code)

What does clang spend on the targets, and how much of it is duplicated
between TUs?  This decides whether the rest is worth doing.

- Build both targets with clang `-O0 -g0` and `-ftime-trace`; split each
  TU's time into lexing/pp, parsing, sema, instantiation, codegen, and sum.
- Duplication: for every `.o`, the COMDAT/weak symbols and their sizes
  (`vnm`); the share of `.text` bytes defined in more than one object, and
  the count of distinct vs total instantiations (from `-ftime-trace`'s
  `InstantiateFunction`/`InstantiateClass` events by name).
- Baselines a result must beat: plain clang, clang with a PCH of the
  common headers, a unity build (all test TUs in one), ccache warm.
- `bench/cxx-profile.py`, numbers in this plan.

Gate: duplicated instantiation + its codegen is a large share of the
build (expectation: most of it for json).  If it is small, stop here and
write down why.

**Results (2026-11-13, x86-64 Linux, clang 23.1.6, libc++, -O0 -g0).**
Sources: nlohmann/json v3.12.0 with json_test_data v3.1.0; libc++ tests at
llvm-project c47d7823685a (the Debian package's commit); amalgamations of
DuckDB 1.4.2 (`duckdb.cpp`, 614k lines; libstdc++, see below), simdjson
4.2.2, Catch2 3.11.0.  `bench/cxx-profile.py build|report|base TARGET`.

| Target | TUs | clang CPU | Frontend | Instantiation | Backend | Instantiations repeated across TUs | COMDAT bytes that are repeats |
|---|---|---|---|---|---|---|---|
| json tests | 71 | 155 s (traced) | 86 % | 53 % (function 36 %, class 17 %) | 14 % | 85.1 % of 228k function, 84.8 % of 505k class | 11.5 of 16.8 MB |
| libc++ tests (7 headers) | 635 of 646 | 191 s (traced) | 92 % | 40 % | 7 % | 39 % of 253k function, 57 % of 242k class | 2.3 of 9.0 MB |
| DuckDB amalgamation | 1 | 81 s | 69 % | 60 % | 31 % | 8 % (inside the TU) | none |
| Catch2 amalgamation | 1 | 3.1 s | 71 % | 47 % | 28 % | 7 % | none |
| simdjson amalgamation | 1 | 0.8 s | 86 % | 13 % | 12 % | 9 % | none |

(Instantiation is clang's `InstantiateFunction` + `InstantiateClass`
totals; `Source`, the time spent reading included files, is 39 % for json
and 2 % for DuckDB.)

- **The gate passes for the multi-TU target.**  In json's tests 85 % of all
  instantiations repeat one done in another TU; one instance of each
  distinct function instantiation costs 72 s of the 303 s spent in them
  (nested time counted in each parent).  69 % of the COMDAT code is a byte
  copy of another object's.
- **A PCH does not remove it.**  PCH of json.hpp + doctest: 1.5 s to build,
  then 101.5 s CPU for the 56 TUs that can use it, against ~130 s without
  (21 % saved).  The 15 others `#define private public` (or configure the
  library) before including json, so they cannot share a PCH.  A unity build
  of json's tests does not compile (`trait_test_arg` and friends defined in
  several TUs): unity is not a free baseline either.
- **libc++'s tests repeat less** (39 % of function instantiations): each
  small TU instantiates its own test types.  46 % of their time is reading
  headers, the part prefix snapshots already handle.  They also show the
  name hazard plainly: `test()` has 84 different bodies in 93 objects (each
  test its own program).  The 11 that do not compile are tests for other
  standard modes or removed features; the harness compiles everything at
  `-std=c++26` without reading lit's `REQUIRES`/`UNSUPPORTED`.
- **Amalgamations measure the other problem.**  One TU has nothing to share
  with another; what is left is instantiation throughput (60 % of DuckDB's
  81 s) and the backend.  DuckDB is the single-TU benchmark, json the
  cross-TU one.

**What the cache key must handle (the hard part, measured).**  Among the
48 json TUs that configure nothing, 120 of 19,569 distinct COMDAT groups
(0.6 %) differ between TUs under the same mangled name, after names of
strings, constant pools and `static` helpers are replaced by their contents.
Every case inspected is one effect: a call emitted as `invoke` with a
landing pad (and the extra block, `jmp` and frame slots that come with it)
in some TUs and as a plain `call` in others, consistent with clang
instantiating a template's `noexcept` lazily, so whether a callee is known
not to throw depends on what the TU did before.  Semantically the same,
bytes not.  Not yet confirmed with a minimal reproduction.  Counting all 71
TUs, 390 groups differ, the rest for real reasons:
- `unit-assert_macro` defines `JSON_ASSERT`, `unit-disabled_exceptions`
  builds with `-fno-exceptions -DJSON_NOEXCEPTION`, `unit-comparison_legacy`
  with a config macro: the same name, other source.  Legal (each test is its
  own executable), and a cache keyed on the name alone would give wrong code.
- Inline templates call `static` functions from the header
  (`nlohmann::detail::little_endianness`): a COMDAT body refers to a
  per-TU copy, so a cached body must keep that reference per TU.

Consequences for the design, now requirements of X4 and X5:
1. The key is the template's identity *as tokens*: the header-cache entry
   its definition came from (vccd already has it; a different macro state
   gives a different entry) plus the arguments, not the mangled name.
2. vcc's own output must not depend on TU order: resolve a callee's
   exception specification before emitting any caller, so the same key
   always gives the same bytes.  We do not reproduce clang's TU-dependent
   variants; the verify mode compares vcc with and without the cache.
3. References from a cached body to internal-linkage entities stay
   symbolic and resolve per TU.
4. DuckDB with libc++ does not compile: glibc's `<langinfo.h>`, pulled in by
   libc++'s `<locale>`, defines `GROUPING`, which DuckDB's parser uses as an
   enumerator.  It builds with libstdc++.

### X1 The `ast.h` split, with C++'s requirements

The split `PLAN-tree.md` and `PLAN-quality.md` leave open (`struct cc` into
language-neutral AST tables and C's parser state), done with section 6 of
the research note as requirements:

- types, records, layout, constant evaluation, mangling (`mangle.c` today)
  shared; C's parser and sema apart;
- every piece of per-TU state follows `ccsnap.c`'s rules (arrays and a heap
  addressed by offset), so a C++ TU's state can be snapshotted too;
- the cc-emit boundary (`cc_run` calling `emit_tu`) becomes a queue of
  (symbol, body, linkage) items, so codegen can be keyed by symbol (X5).

Gate: no output change for C (`vcc-same.py`, byte for byte), budget 1 %
in `vbench.py gate`.

### X2 Lexer and driver mode

- The lexer needs no C++ mode, which keeps one lexed file shareable between
  C and C++ TUs in vccd: `::` and raw strings are already lexed, digit
  separators go through C23's `lex_file_digit_separators_remove`, keywords
  are identifiers to the lexer.  `.*`, `->*` and `<=>` come out as two
  tokens, and `<::` as `<:` `:`; the C++ parser joins (or splits) them when
  they are adjacent in the same file (`off + len == next.off`), which is
  maximal munch and, like clang, never joins across a macro boundary.
- pp: `__cplusplus`, `__has_cpp_attribute`, C++ predefines taken from clang
  once (`gen-ppdata.sh` for `-x c++`), libc++'s include paths.  The C++
  predefines are the C base plus a table per standard: about 75 `__cpp_*`
  lines, `__GNUG__`, `__GXX_*`, `__EXCEPTIONS`, `_GNU_SOURCE`,
  `__STDCPP_*`, minus `__STDC_VERSION__` and `__GNUC_STDC_INLINE__` (and
  `linux`/`unix` in strict modes); `-fno-exceptions` and `-fno-rtti` remove
  their four.  `true`/`false` evaluate in `#if`.
- Driver: `-x c++`, `.cpp`/`.cc`, `-std=c++20`, `-fno-exceptions`,
  `-fno-rtti`.

Gate: `vcc-diff.py` parity (`-E`, `-dM`) on every TU of both targets.

### X3 C++ without templates

Namespaces, classes (members, access, static members, nested types),
constructors and destructors with implicit special members, references,
overloading and its resolution, operators, `new`/`delete`, single and
multiple inheritance, virtual functions (vtables, Itanium ABI), `constexpr`
evaluation, lambdas (closure types), range-for, `auto`.

Validation: a small suite of our own under `tests/cxx/`, each case checked
against clang (layout, mangled names, which overload, constexpr value, run
output), as `vcc-ctests.py` does for C.  Mangling has its own harness: every
symbol of every clang-built object of the targets demangled and re-mangled
by vcc.

### X4 Templates, with instantiation as a recorded unit

Class, function, alias and variable templates; partial specialisation;
deduction (including CTAD); SFINAE; `requires`/concepts as libc++'s headers
use them; two-phase lookup; the type-trait builtins libc++ calls
(`__is_same`, `__remove_cvref`, ...: list from libc++'s headers, compared
with `__has_builtin` as `vcc-builtins.py` does).

The design point of the plan: **an instantiation is a function of its
inputs**, and records them.  The inputs are the template's identity, the
arguments, and every declaration its lookups found (names, the overload
sets and specialisations that were visible).  The result is its AST and
the instantiations it requested.  Nothing else in the TU's state may be read
or written.

Gate: target 1 passes under vcc with libc++, no tests marked unsupported
beyond what clang marks; mangled names identical to clang's.

### X5 The two caches in vccd

1. **Instantiation cache.**  Key: template identity + arguments.  Entry: the
   recorded inputs and the result.  A TU may take an entry only if its own
   lookups of the recorded names give the same declarations (checked by
   identity, so cheap).  The ODR says they will; the check catches the
   programs that break it instead of trusting them.
   `VCC_CXX_IC=verify` instantiates every time and compares with the entry;
   any difference is a report with both inputs.
2. **Codegen by symbol.**  A COMDAT function's machine code, keyed by
   mangled name + a hash of its body and flags, emitted once per build and
   copied into each object that needs it.  Objects stay complete (archives,
   make and the linker see nothing new).  `VCC_CXX_CG=verify` emits again and
   compares bytes.
3. Prefix snapshots (`ccsnap.c`) for C++ TUs, as for C.

Each cache can be turned off alone (`=0`) so its share can be measured.

Gate: verify modes report zero mismatches on both targets; objects
byte-identical with caches on and off.

### X6 Target 2 and the verdict

nlohmann/json's test suite built by vcc (through vccd, `make -j 6`), linked
by clang or vlink, every test passing.  Then the numbers against X0's
baselines, each cache off and on:

| Build | Cold wall | CPU |
|---|---|---|
| clang | | |
| clang + PCH | | |
| clang unity | | |
| vcc, no caches | | |
| vcc + snapshots | | |
| vcc + instantiation cache | | |
| vcc + codegen by symbol | | |

Verdict written here: does the design win for C++, by how much against the
best clang baseline, and which cache carries it.

### X7 Later, only if X6 wins

Exceptions and RTTI, `-g` (DWARF types deduplicated the same way as code),
libstdc++, -O2 through VIR, a third target that has a unity+PCH build of its
own.

## Open questions

- **Where the instantiation's recorded inputs live.**  Recording every
  lookup may cost more than it saves; X0's numbers say how much headroom
  there is.  A coarser key (the set of headers visible) is the fallback.
- **Point of instantiation.**  Function templates instantiate at the end of
  the TU in clang; an entry taken early must give the same result.  The
  verify mode is how we find out.
- **Diagnostics from an instantiation** ("in instantiation of ..." notes)
  must come out per TU even when the entry is cached: store them with it.
- **constexpr cache.**  Probably the same structure (a function of its
  inputs); measure in X0 before adding a third cache.

## Status

- X0: done (2026-11-13), gate passed for json; results above.
- X2 (pp and driver part): done for json (2026-11-13).  `-x c++`, the C++
  suffixes, `-std=c++NN`/`gnu++NN` (default gnu++17), `-fno-exceptions`,
  `-fno-rtti`, `-stdlib=libc++` (its `include/c++/v1` first, spelled as
  clang++ does), `-nostdinc++`.  Predefines and the `__has_*` answers in C++
  are generated from clang++ once (`src/tools/gen-cxxpredef.py` ->
  `frontend/pp/ppdata-cxx.inc`, `ppdata-cxx-has.inc`; corpus: libc++, json,
  doctest, and the queries in glibc's and clang's headers, wrapped ones
  too); `__has_cpp_attribute` replaces `__has_c_attribute`; `true` is 1 in
  `#if`; `#pragma clang deprecated/restrict_expansion/final` are dropped by
  `-E` as clang does (this also fixes C).  `bench/cxx-json.py --stage pp`:
  all 71 json TUs give clang++'s `-E -P` token stream.  The parser joins of
  `.*`, `->*`, `<=>` are for X3/X4.  libc++ is reused as is; a library of
  our own may come later.
- X3/X4 (parsing, first form): all 71 json TUs parse (2026-11-13;
  `bench/cxx-json.py --stage syntax`: 60.3 M tokens, 71/71).  `src/frontend/cxx/`
  is a recognizer with name lookup: the TU's tokens in one array (backtracking
  by index, `>>` splits through an undo log), scopes for namespaces (inline
  ones and using-directives), classes (bases; a dependent base is skipped by
  unqualified lookup, two-phase), templates (specializations recorded with
  their template), member bodies parsed when the outermost class is complete,
  C++20's implicit typename, deduction guides, structured bindings, the
  `<::` rule, user-defined literals.  No AST, no semantic checks yet: it
  accepts code clang would reject on types.  The rules it needed are in
  `src/test/cxx/parse` (`bench/cxx-parse-tests.py`, each case checked with
  clang).  Not a speed number yet: parsing unit-32bit takes 0.13 s against
  clang -fsyntax-only's 1.17 s, but clang also checks and instantiates.
  Development started at 0.1 % of the tokens; libc++ was 80 % of the work.
- X1, first step (2026-11-13): one AST for C and C++.
  `src/include/frontend/ast.h` holds the side-table records codegen reads
  (`cstr label attrs asmop casm`, `i128`) and `struct ast`: the tables and
  TU-wide values code generation reads, in the order
  `doc/quality/MEASURE-ast-split.md` measured (per-node tables first, `o`
  next to `ty`).  `struct cc` holds it as its first member through a union
  with an identical anonymous struct, so `c->syms` and `c->ast.syms` are one
  field and no code generation line changed; `_Static_assert`s keep it at
  offset 0 and under 504 bytes.  The C++ front end is compiled in the `cc`
  module and `cxx_run` gets a `struct cc` as its AST: the same type
  interning, record layout and constant code, and later `emit_tu`.
  Verified: `bench/vcc-same.py --base` (the previous commit) 15 210/15 218
  outputs identical, the 8 others `-E` line markers naming the base's
  `src/headers/arm_neon.h` path; `vcc-x86.py --suite vcc` 320/320; vcc-diag
  229/229; every file compiles alone (`VCC_UNITY=`; that build's link
  stops at `vir_failure_table`, defined in a codegen header this change does
  not touch).  `vbench.py gate` cannot read instruction counters on this
  host (its x86-64 Linux setup); the arm64 measurement in
  MEASURE-ast-split.md showed the embedding removes instructions.
- X1, second step (2026-11-13): the C++ parser fills the shared AST
  (`src/frontend/cxx/types.c`).  Decl-specifiers and declarators give
  types through C's code (`cc_type_*`; C's inside-out declarator rule, a
  parenthesized declarator parsed after its suffixes); references are
  `TY_PTR` with `ext` 1 or 2 (distinct when interned, passed as pointers);
  wchar_t/char8_t/char16_t/char32_t have C kinds of their size for now.
  Classes become records laid out by `cc_layout_record` when C's rules are
  C++'s (no bases, no virtual functions, every member typed; an empty class
  is 1 byte), forward declarations create the incomplete record, anonymous
  members are unnamed fields, and a class with a member that cannot be
  typed is never laid out.  Namespace-scope variables and functions get
  symbols.  unit-32bit: 5 891 of 20 147 declarations typed, 305 of 1 789
  classes laid out (skipped: 1 236 templates, 146 specializations, 47 with
  bases, 10 virtual, 45 with an untyped member), 2 815 symbols.
  `bench/cxx-layout.py`: every class laid out, with a name clang can
  resolve, is a `static_assert` on sizeof and alignof compiled by clang++
  with the TU: 20 242 over the 71 TUs, all agree.  C unchanged
  (`vcc-same.py` as in the first step; vcc-x86 320/320; vcc-diag 229/229).
- X4, first form (2026-11-14): class and alias templates are instantiated
  (`src/frontend/cxx/instantiate.c`).  The token array is immutable (`>>`,
  `>=`, `>>=` are split when read; the expression parser joins adjacent
  pieces), so an instance re-parses its template's tokens: a scope binds
  each parameter to its argument and the class head and body (an alias's
  type-id) are parsed again with no template around; function bodies are
  skipped.  Lazy, as C++ is: naming `X<args>` makes an incomplete record and
  a cache entry; the body is parsed when the class must be complete (a
  member or variable by value, `X<args>::name`).  Eager instantiation ran
  valarray's expression templates into the depth limit thousands of times.
  The cache, keyed by the template and its complete argument list, is the
  first form of X5's: explicit specializations are entered there (as written
  and with defaults completed), and default arguments may come from an
  earlier declaration of the template.  Simple constants: an integer
  literal, `true`/`false`, a value template argument, a const/constexpr
  integer initialized with one (array bounds, value arguments).  Answered
  "unknown" instead of guessed: templates with partial specializations
  (matching is not written), packs, template template parameters, other
  constant expressions.  `bench/cxx-layout.py`: 26 014 layouts over the 71
  TUs, all agree with clang (instances spelled `ns::X<int, ns::A<int>>`);
  `bench/cxx-parse-tests.py` also has clang check its cases' layouts; 71/71
  parse under ASan.  unit-32bit: 59 instances, 433 of 1 876 classes laid out.
- X4, partial specializations (2026-11-14): each records its parameters,
  its argument pattern's tokens and its class-key.  To match, the pattern is
  parsed once with its parameters bound to placeholders (a unique record
  type per type parameter, a marked value per value parameter); a structural
  matcher deduces them against the arguments through cv-qualifiers,
  pointers, references, arrays, function types and other templates'
  instances (their cache entries: template and arguments).  Several matches:
  the most specialized by partial ordering (each pattern matched against the
  other's); ambiguous, a pattern that cannot be evaluated (enable_if and
  other unknown arguments), or a parameter not deduced: unknown.  The chosen
  specialization's class is re-parsed with the deduced bindings, cached under
  the primary's key; explicit specializations still come first.  Also: a
  using-declaration now carries its entity's type (std::uint64_t was
  unknown), and an array member of an incomplete class is never laid out.
  `bench/cxx-layout.py`: 26 823 layouts, all agree with clang; json 71/71
  under ASan; 12 parse cases (ordering, nested and value patterns,
  std::array).  unit-32bit: 82 instances, 2 partial specializations
  matched, 8 unknown.
- Base classes (2026-11-14), Itanium ABI without virtual functions: bases
  in order, each at its alignment; an empty base at offset 0 (empty base
  optimization; two subobjects of one type at one address are refused, not
  laid out); a POD base takes its size, a non-POD base its data size, so the
  derived class's members reuse its tail padding.  POD for layout is
  approximated as trivial and standard-layout: no user-provided special
  member, no default member initializer, one access for all data members,
  POD bases and members, data in at most one class of the hierarchy.  A
  template-id base outside a template is an instance, completed.  Classes
  with attributes that change layouts (aligned, alignas, packed,
  no_unique_address, ms_struct) are not laid out yet.  `bench/cxx-layout.py`:
  29 722 layouts, all agree with clang; json 71/71 under ASan; 13 parse
  cases (tail padding by each POD rule).  unit-32bit: 501 of 1 939 classes
  laid out, 118 instances.
- Packs (2026-11-14): a pack parameter binds to the remaining arguments; an
  expansion (template arguments, base clauses) is re-parsed once per element
  with its packs bound to that element; sizeof..., template template
  arguments, `__make_integer_seq`, `__type_pack_element`, trailing packs in
  partial specialization patterns (also nested: `iseq<T, Is...>`).  wchar_t
  and char8/16/32_t became distinct types.  `bench/cxx-layout.py`: 32 028
  layouts, all agree with clang; 14 parse cases (a libc++-style tuple from
  leaf bases and an integer sequence).
- Next: the semantic analysis the 30x estimate depends on (expressions'
  types, overload resolution, function template deduction, instantiating
  function bodies), measured against clang as it grows; no_unique_address
  (libc++'s compressed pairs); vptr layouts.  Code: the shared builders move to a
  module of their own taking `struct ast *` (cxx/ is compiled in the cc
  module for now).  X5-X7: not started.

### Status 2026-11-14: expression types (branch cxx/sema)
Parameters and expressions carry types (`typing.c`); unit-32bit types 319 of
612 expression statements outside templates.  CPU, -fsyntax-only, user+sys:
unit-32bit vcc 0.12 s vs clang 0.80 s (6.6x), unit-allocator 0.12 s vs 0.99 s
(7.9x).  vcc does not yet instantiate function bodies or resolve overloads
fully, so the ratio will fall; the 4.5x threshold still has headroom.

### Status 2026-11-14 (2): deduction
Function templates keep their declaration; the first call parses the
signature with placeholder parameters, each call deduces from the argument
types (by value: decay and top-level cv; T&& with an lvalue: T = A&) and gets
the return type by re-parsing the signature with the arguments bound.  Fixed
on the way: `typename X::y` was always dependent, so instances lost such
member types; `f(x);` with f a function template parsed as a declaration.
unit-32bit: 337/378 expression statements typed, 539 classes laid out (was
501), 228 instances (was 119); json: 34,617 layouts clang agrees with (was
32,028), 71/71 parse.  Not yet: packs, explicit template arguments, default
template arguments, deduction against base classes.

### Status 2026-11-14 (3): bodies on use
Function template bodies are instantiated when a call deduces them; class
template member bodies when a call picks them; constructors and destructors
are declared (under names no source spells) and used by variable definitions
and T(args).  unit-32bit: 43 template bodies + 103 member bodies, none
failed, 0.12 s.  The measure of what is left: clang -O0 emits 1,692
functions for unit-32bit (1,529 linkonce_odr), so vcc instantiates about a
tenth of what code generation will need.  Missing: implicit special members,
calls whose overload vcc cannot choose, operators on dependent operands
inside instances, explicit template arguments, packs.

### Status 2026-11-14 (4): calls
Explicit template arguments, clang's type transform builtins, trailing
return types, const/non-const member overloads (by the object's or this's
constness), constructor templates and member templates of instances.
unit-32bit: 1170 of 1432 calls typed; 160 function template bodies and 147
member bodies instantiated, none failed (clang emits 1,692 functions); json
71/71 parse, 38,352 layouts clang agrees with; 0.12 s.  The largest group of
untyped calls (84) is SFINAE templates: an unnamed parameter whose type is
enable_if_t<cond, int>, which needs the parameter's type parsed with the
others bound and `X<T>::value` evaluated as a constant.  Then: implicit
special members, packs, closure types, conversions in overload resolution.

### Status 2026-11-14 (5): constant expressions
Expressions carry an integer constant value next to their type: literals,
const/constexpr integers and value parameters, unary and binary operators
(&& and || decided by a constant left operand), ?:, casts, sizeof/alignof of
types vcc sizes, and the type-trait builtins that read only a type's form
(__is_same, __is_integral, ...).  Template arguments and constant members
(`static constexpr bool value = ...`) take it.  Fixed on the way: an alias
template's instance had no members at `::` or as a base; a rewound tentative
parse left dependent_seen set.  Negative template arguments stay unknown (a
value parameter's type is not recorded).  json: 42,955 layouts clang agrees
with (was 38,352), 71/71 parse; unit-32bit classes laid out 662 (was 540).

### Status 2026-11-14 (6): default template arguments and SFINAE
After deduction the remaining parameters take their default arguments, and
an unnamed value parameter's type is formed with the earlier ones bound:
`typename X::type` naming no member of a complete class X removes the
candidate (enable_if).  Alias instances remember that failure for cache
hits.  sizeof(expr) is a constant.  unit-32bit: 1428 of 1721 calls typed;
267 function template bodies and 195 member bodies instantiated, none
failed (clang emits 1,692 functions); 0.12 s.  json: 47,236 layouts clang
agrees with, 71/71 parse.  Tests: ok-constant-expressions, ok-sfinae-deduction.

### Status 2026-11-14 (7): auto
`auto x = init` (with const, &, &&, *) is deduced from the initializer as a
one-parameter template deduction; a range-for's variable from *begin(range)
(arrays, classes with begin() returning a pointer or an iterator with
operator*).  Before this no auto variable had a type.  unit-32bit: 1943 of
2450 calls typed; 441 function template bodies and 196 member bodies, none
failed (clang emits 1,692 functions); 0.12 s.  json: 47,512 layouts clang
agrees with, 71/71 parse.  Not deduced yet: auto return types (the body's
return statements), structured bindings, auto x{...}, generic lambdas.

### Status 2026-11-14 (8): auto returns, lambdas, decltype
auto return types (the instantiated body's first return), closure types for
lambdas with known parameter types (operator(), return from the body),
decltype of names, calls and other lvalues (with reference collapsing), and
a deduced template whose bound parameters match the arguments exactly wins
over the others.  Instances whose member typedefs do not resolve are not
asserted against clang (libc++ names ill-formed ones where that is harmless).
unit-32bit: 2111 of 2739 calls typed; 505 function template bodies and 210
member bodies, none failed (clang emits 1,692 functions); 0.13 s.  json:
46,091 layouts clang agrees with, 71/71 parse; parse tests 17/17.
Largest untyped groups now: function objects' operator() with decltype
return types over their parameters, begin(x) through ADL, libc++'s
strong_order/weak_order customization objects, packs.

### Status 2026-11-14 (9): only what C++ instantiates
static_assert is evaluated (statistics; an error only with
VCC_CXX_STATIC_ASSERT=1, which the ok-check-* tests set).  libc++'s own
assertions found three over-instantiations, now fixed: a body is instantiated
only for the function a call selects (not for every viable candidate), not
from an unevaluated operand (decltype, sizeof, noexcept, requires) unless its
return type is auto, and not from the discarded statement of an if constexpr
whose condition is a known constant.  A T&& parameter does not bind an lvalue,
a non-const T& not an rvalue (std::forward's two overloads).
unit-32bit: static_asserts 69 true, 0 false (was 16), 116 not evaluated;
168 function template bodies and 180 member bodies (the earlier 516 + 210
included bodies C++ does not instantiate); 1911 of 2542 calls typed; clang
emits 1,692 functions.  json: 44,885 layouts clang agrees with, 71/71 parse;
parse tests 18/18.  Next: choosing among several viable candidates (partial
ordering of function templates, conversion ranks), which is where bodies are
now not instantiated; packs.

### Status 2026-11-14 (10): overload ranking; what blocks next
Candidates are ranked by each argument's conversion (exact, standard,
perhaps through a class, none), then non-template over template and f() over
f() const.  unit-32bit: 1902 of 2543 calls typed, 157 + 180 bodies, no false
static_assert; json 44,418 layouts clang agrees with, 71/71 parse.
The largest groups of untyped calls are libc++'s C++20 customization points
(ranges::begin's `begin(__t)`, `__go(..., __priority_tag<N>())` in
strong_order and friends): their overloads are chosen by requires-clauses and
concepts, which vcc parses and does not evaluate.  Next: concept-ids and
requires-clauses as constant expressions (a concept's definition with its
arguments bound; a requires-expression is true when each requirement's
expression has a type), then packs.  VCC_CXX_WHY_TOKENS=name prints the
tokens around each untyped call of that name.

### Status 2026-11-14 (11): concepts
A concept-id is a constant: the concept's constraint-expression with its
arguments bound (cached per argument list).  A requires-expression is true
when every requirement holds, false when one is ill-formed for sure (no
function fits, here or by ADL; no such member; a member of what is not a
class; typename X::y missing; a nested requires that is false; a
return-type-constraint that is not satisfied), else not known.  Function
templates check constrained parameters (template<C T>), the header's
requires-clause and a trailing one when deducing: false removes the
candidate, not known leaves the call untyped.  Test ok-check-concepts.
unit-32bit: 1644 of 2023 calls typed (81 %), 123 + 180 bodies, no false
static_assert; json 48,962 layouts clang agrees with (was 44,418), 71/71
parse; parse tests 19/19; 0.14 s.

### Status 2026-11-14 (12): variable templates, trivial-type traits
Variable templates evaluate (primary, partial and explicit specializations,
through the class templates' matcher; T[N] with N deduced: not yet), and the
traits that ask what a type does trivially answer for scalars, arrays of
them and classes vcc laid out as POD.  libc++'s own static_asserts on
unit-32bit: 160 true, 0 false, 16 not evaluated (was 62 / 0 / 116 two steps
ago).  1708 of 2081 calls typed; 123 + 182 bodies; json 54,963 layouts clang
agrees with, 71/71 parse; parse tests 21/21 (ok-check-variable-templates,
ok-check-traits).  Untyped calls now: __go(..., __priority_tag<N>()) (31),
__to_address ambiguous or unknown (43), function objects' operator() (45),
__promote_impl (14), iter_move (11); "callee unknown" 115.

### Status 2026-11-14 (13): json itself is instantiated
Until now `nlohmann::json` had no type: basic_json's template template
parameters have defaults (ObjectType = std::map), which were not recorded,
and `using json = basic_json<>;` names the instance while the template is
only declared.  Now: defaults of template template parameters; an instance
named before its template is defined is bound when completed; a declared
template has a home scope for its defaults; an instance is completed in a
tentative parse too (J::size_type n{}; was misread); class template members
defined outside the class are instantiated on use (name, parameter count,
const).  unit-32bit: 154 function template + 321 member + 23 out-of-class
bodies (was 123 + 182 + 0), 2286 of 2703 calls typed, static_asserts 178
true / 0 false / 22 not evaluated; 0.16 s (clang -fsyntax-only 0.80 s);
unit-allocator 0.25 s (0.99 s).  json: 61,472 layouts clang agrees with,
71/71 parse; parse tests 21/21.

### Status 2026-11-14 (14): enumerators, nullptr, per-parameter deduction
Enumerators have their enumeration's type and values (template arguments
carry the enumeration: X<(E)0>); nullptr has a type of its own; deduction
works parameter by parameter (one of unknown type is skipped, not fatal);
between templates that fit equally the one with fewer bare-T parameters wins,
then the declaration that has a body; a member operator sees its object's
constness.  The test's own statements (json j; j["a"] = 2; json::parse(..);
json::from_bjdata(..)) now resolve.  unit-32bit: 158 + 330 + 23 bodies, 2314
of 2727 calls typed, static_asserts 197 true / 0 false / 22 not evaluated,
0.16 s; json 62,409 layouts clang agrees with, 71/71 parse; parse tests 21/21.
VCC_CXX_CAND=name traces each candidate of calls to that name.
Next: libc++'s _And/_Or/_Not over packs (std::__to_address's two overloads
are both accepted: 64 calls), __go(..., __priority_tag<N>()), out-of-class
members that share a parameter count, then the rest of packs.

### Status 2026-11-14 (15): specializations matched by substitution
A partial specialization whose pattern has an argument that cannot be deduced
(decltype((void)expr), void_t<typename T::x>, a default that uses earlier
parameters) is matched by deducing from the rest, substituting, and comparing:
an ill-formed substitution is no match, one vcc cannot type is "not known".
Patterns shorter than the parameter list take the primary's defaults; a
pattern may name template template parameters (is_basic_json).  A function
template constraint vcc cannot evaluate is "not known", no longer accepted.
Instances are created in tentative parses too.  Fixed on the way: a default
value argument left its type field unset (specializations on true/false
stopped matching once enumeration types were compared).
unit-32bit: partial specializations 154 matched, 10 unknown (was 137 / 104);
static_asserts 177 true, 0 false, 22 not evaluated; 166 + 337 + 23 bodies;
2483 of 2884 calls typed; 0.16 s.  json: 66,891 layouts clang agrees with,
71/71 parse; parse tests 22/22 (ok-check-detection).

### Status 2026-11-14 (16): relation traits, explicit packs
__is_base_of, __is_convertible and __is_constructible answer where no
class's constructors or conversion functions are needed (scalars, pointers,
derived to base); a bound pack of types expands in a builtin call
(__is_constructible(T, Args...)); a function template whose last parameter is
a pack is called with explicit arguments (libc++'s _And = decltype(
__and_helper<_Pred...>(0))), a substitution failure in its signature makes it
not viable, and f(...) ranks after any real parameter.  Of 19 libc++ trait
assertions surveyed (is_class, _And, _Not, is_convertible, decay_t, ...) 16
evaluate true, 0 false; _Or, common_type_t and __underlying_type do not yet.
unit-32bit: 179 + 337 + 23 bodies, 2503 of 2885 calls typed, static_asserts
177 true / 0 false / 22 not evaluated, 0.16 s; json 72,738 layouts clang
agrees with, 71/71 parse; parse tests 22/22.

### Status 2026-11-14 (17): arrays, reference binding, the first break in from_bjdata
T[N] with N deduced (function templates and specializations); f(C&) over
f(const C&) for an object that is not const and f(const T&) as the more
specialized template for one that is; deduction never converts a class (a
pattern that matches neither the argument nor a base fails: std::begin's
initializer_list overload no longer leaves begin(v) undecided);
using-declarations carry a function template's data; a comma inside
decltype( ) is the operator; an expression ill-formed for sure in a signature
makes the candidate not viable.  Fixed: X<args>::m with no instance (a
specialization vcc could not decide) looked in the primary template and
returned its constants; a pack expansion's first, unexpanded parse marked the
enclosing type dependent.
unit-32bit: 189 + 343 + 23 bodies, 2615 of 3056 calls typed, no false
static_assert; json 73,003 layouts clang agrees with, 71/71 parse; parse
tests 22/22.
What stops json's own code: from_bjdata -> input_adapter(container) ->
input_adapter(first, last) -> iterator_input_adapter_factory<It>, whose
specialization asks std::iterator_traits<It>::value_type; libc++ (C++20)
defines __iterator_traits by partial specializations constrained with
concepts, which vcc does not evaluate for class templates yet.  Next:
constraints on class template partial specializations (constrained
parameters and requires-clauses), then binary_reader's members.

### Status 2026-11-14 (18): constrained class specializations
A class template partial specialization's constraints (template<C T> struct
X<T>, requires-clauses) are evaluated with the deduced parameters: not
satisfied removes it, not known leaves the instance undecided; of two with
the same pattern the constrained one is the more specialized.  Types are
built in tentative parses too (sizeof(X<int *>::type) was misread as an
expression).  std::iterator_traits<T*>, <vector::iterator>,
<string::const_iterator> evaluate.
unit-32bit: 188 + 343 + 23 bodies, 2657 of 3089 calls typed, no false
static_assert, 0.19 s; json 71,556 layouts clang agrees with, 71/71 parse;
parse tests 22/22.  Undecided now: constraints of std::__common_ref and
__common_reference_sub_bullet1 (67), __iterator_traits for one iterator type
(5), so from_bjdata still stops at iterator_input_adapter_factory.

### Status 2026-11-14 (19): json's own code is instantiated
json's tests are built as C++11 by its own CMake (the harness's -std=c++20
was this plan's choice); in C++11 libc++ uses its __cxx03 headers, without
concepts.  What stopped from_bjdata, and is fixed: decltype(expr):: as a
qualifier (C++03 detection: decltype(test<T>(nullptr))::value); an auto
f() -> decltype(invalid) lost its "not viable" in a second parse; the invalid
flag of one call was cleared by the next call in the expression; a call
returning T&& is an xvalue (binary_reader(std::move(ia), ...)); templates
declared inside an instance are recorded (basic_json::binary_reader<T>); an
instance's injected class name is a type argument (reader<J, I> inside
J<long>); a template template parameter bound to a template keeps that
template's identity (nlohmann's detector: is_detected_exact<bool, Op, SAX>
was false).  Members of a partial specialization defined outside the class
are not bound to the primary's instances (vector<bool, A>::push_back made
vector<bool, bool>).
unit-32bit, C++11: 254 function template + 409 member + 37 out-of-class
bodies (lexer 41, binary_reader 31, json_sax_dom_parser 26, basic_json 18),
3861 of 4427 calls typed, static_asserts 108 true / 0 false / 13 not
evaluated, 6 instances failed, 0.13 s (clang -fsyntax-only 0.58 s).  C++20:
223 + 486 + 38, 0.17 s (clang 0.80 s).  clang -O0 emits about 1,670
functions for the file in either mode.  json: 96,270 layouts clang agrees
with, 71/71 parse; parse tests 23/23 (ok-check-detector).

### Status 2026-11-14 (20): to_json detection, klassers kopiering/flytt

libc++:s `swap` och nlohmanns `has_to_json`-kedja (basic_json:s
konverterande konstruktor) avgörs nu.  Det som saknades:
- `__is_constructible(T, T&&/const T&)` och `__is_assignable(T&, ...)` för
  klasser: deklarerade kopierings-/flyttkonstruktorer och `operator=`
  (`= delete`, `= default`, implicita: baser och datamedlemmar).
- Funktionsmallskandidater vars parameterform inte kan passa sorteras bort;
  platshållare för paket och mallmallparametrar; `R(Args...)`-mönster.
- Parametrarna är i scope redan i `noexcept(...)`, och deklareras innan den
  tolkas (det `noexcept` instansierar skrev över `c->fn_*`, så parametrarna
  i den efterföljande `-> decltype(...)` saknade typ).
- Giltighetsflaggorna (`einvalid`, `eunsure`, `member_missing`) läcker inte
  längre ut ur en klassinstans som kompletteras mitt i ett uttryck, och inte
  från ett paketmönster som tolkats oexpanderat.
- Ett anrop på platshållare i en partiell specialiserings mönster
  (`X<T, decltype(f(std::declval<T>()))>`) substitueras vid matchning i
  stället för att alltid passa; `a, b` med otypat `a` är "okänt"; `obj(args)`
  utan passande `operator()` är ogiltigt.
- `bench/cxx-layout.py` lägger den sist byggda klassen först (basic_json och
  dess komparator behöver varandra; clang måste börja utifrån).
unit-32bit, C++11: 273 funktionsmallar + 451 medlems- + 42 out-of-class-
kroppar, 4646 av 5161 anrop typade, static_asserts 111 sanna / 0 falska / 13
ej utvärderade, 0 misslyckade instanser.  json: 113 380 layouter som clang
håller med om (0 fel), 71/71 parse; parse-tester 28/28 (ok-check-serializer,
ok-check-detection-calls, ok-check-detection-comma, ok-check-pack-calls).
Kvar före mätningen mot clang -O2: resterande otypade anrop (none viable 138,
callee unknown 135, unbound 104), kodgenerering för C++ från det delade
AST:t, länkning och körning av testerna.

### Status 2026-11-14 (21): libc++-behållarna får layout, json(int) avgörs

Mätt med sonder (`static_assert` mot json.hpp/libc++ som clang godkänner,
`VCC_CXX_STATIC_ASSERT=2` rapporterar varje falsk/okänd och fortsätter):
av 36 + 39 + 12 påståenden om json:s byggstenar var 11 rätt i början av
steget; nu är alla utom fyra avgjorda och lika med clang.
- Layout: `[[no_unique_address]]` (tom medlem tar ingen plats; en som inte
  är POD för layout lämnar sin svansutfyllnad), packade klasser, namnlösa
  klasser efter attribut, bitfältsbredder som konstantuttryck,
  `__builtin_offsetof` som konstant, `__datasizeof`, `__is_final`.
  `std::string` 24, `vector` 24, `map` 24, `unique_ptr` 8, `shared_ptr` 16,
  `basic_json` 16: som clang.
- Egenskaper: `__is_constructible`/`__is_convertible` för klasser genom
  överlagringsupplösning av konstruktorerna (explicit, borttagna, implicita
  default/kopia/flytt); användardefinierad konvertering som egen rang;
  en klass utan konverteringsfunktion konverterar inte till skalär/pekare;
  ett heltal av en typ (inte ett uttryck) är ingen nollpekarkonstant.
- Konstanter: `T(x)`/`T()` av heltalskonstanter, jämförelser med osignerade
  operander, `constexpr`-variabler är const, `static constexpr auto`.
- Upplösning: giltighetsflaggorna hålls isär per anrop (kandidater som inte
  deduceras, klasser och signaturer som instansieras på vägen); ett gammalt
  "ingen passar" från ett inre anrop gjorde `obj(args)` ogiltigt.
  `T&` (inte const) kompletterar inte parameterns klass
  (`basic_string<unsigned char>` är ogiltig i libc++ och får inte röras).
unit-32bit, C++11: 280 funktionsmallar + 422 medlems- + 38 out-of-class-
kroppar, 4984 av 5497 anrop typade, 0.12 s.  json: 130 116 layouter som
clang håller med om (0 fel), 71/71 parse; parse-tester 33/33.
Kvar före mätningen mot clang -O2: `begin/end`-sonderna, strängoperatorer
(`operator+`, `==` som fria mallar), vptr-layout (undantagshierarkin),
kodgenerering för C++, länkning och körning.

### Status 2026-11-14 (22): operatorer, dolda vänner, medlemsmallar med explicita argument

Sonderna (nu även `std::string`/`vector`-operatorer, `ostream <<`,
doctest och json på användningsnivå: `j[0]`, `j == 1`, `j.at("k")`,
`json{1, 2, 3}`, `os << j`, `j = 1`) stämmer med clang utom
`j.get<int>()` och doctests `has_insertion_operator`.
- Operatorer: ADL i operandernas klassers namnrymder; dolda vänner
  (funktioner deklarerade `friend` i klassen hålls per klass och hittas
  genom ett argument); operandernas värdekategorier följer med (ett
  lvalue-`ostream` är inte libc++:s rvalue-ström-överlagring); vänster
  operand som inte är en klass; en operator som ingen funktion tar på en
  klass utan konverteringsfunktion är ogiltig (`__is_ostreamable`).
- Partiella specialiseringar väljs när instansen måste vara komplett, inte
  när den namnges (`std::_And<A, X<args>>` rör aldrig `X<args>` när `A` är
  falsk): tidigt val fick `__is_ostreamable` att fråga efter sig själv
  utan slut (120 s och 12 GB på en TU; nu 0,16 s).
- `x.f<args>(...)` med medlemsmallar (json:s `get<T>()` genom
  `priority_tag`); en explicit specialiserings scope är dess egen klass
  (`tag<4>` konverterar till `tag<0>`); en ogiltig aliasinstans är ogiltig
  även ur cachen; paketsubstitution avbryts vid första ogiltiga element.
- Konverteringsfunktioners måltyper sparas per klass:
  `is_assignable<int&, const std::string>` är falsk i stället för okänd.
unit-32bit, C++11: 0,16 s; json: 145 462 layouter som clang håller med om
(0 fel), 71/71 parse; parse-tester 37/37.
Kvar före mätningen mot clang -O2: `j.get<T>()`, vptr-layout,
kodgenerering för C++ från det delade AST:t, länkning och körning.

### Status 2026-11-14 (23): mätt gräns för 30x med dagens front end

Hela sviten (71 TU:er, en i taget, `-fsyntax-only`, CPU-sekunder på den
här maskinen), med dagens C++-front end (tolkning, instansiering av det
som nås, typning; ingen AST för kroppar, ingen kod):

| Mätning | Värde |
|---|---|
| clang -O2 (baslinjen) | 243 s; 30x är **8,1 s** |
| vcc, C++20-flaggor (som baslinjen) | **13,7 s** |
| vcc, C++11 (json:s egen CMake) | 10,2 s |
| vcc, en TU med bara de delade huvudena (json, doctest, libc++) | 0,155 s; x 71 = **11,0 s** av de 13,7 |
| vcc, det som är TU:ernas eget (13,7 - 11,0) | **2,7 s** |

Slutsatser som mätningen bär:
- Utan delning mellan TU:er går 30x inte: front end ensam tar 13,7 s, mer
  än hela budgeten på 8,1 s, och den instansierar ännu bara ca 45 % av
  funktionerna clang emitterar (747 av ca 1 670 i unit-32bit).
- Med huvudena tolkade en gång (vccd, X5) är front end ca 0,16 + 2,7 =
  **2,9 s**; då återstår ca 5,2 s av budgeten för resten av
  instansieringen, AST, kodgenerering och objektfiler för 71 TU:er.  De 23
  TU:er som konfigurerar om json delar bara libc++ och kostar mer än så.
- Mätningen ände-till-ände (kompilera, länka, köra testerna) finns inte:
  kodgenerering för C++ är inte påbörjad.

Samma steg: operatorer skrivna som anrop (`operator<<(a, b)`), verklig
tvetydighet (två kandidater som inget slår och som konverterar ett argument
till olika typer i samma rang) är ogiltig, och en gemensam returtyp hos
kandidater vcc inte kan ordna gör inte längre anropet giltigt (vcc sade att
doctests `has_insertion_operator<int>` var sann; clang: falsk).
`j.get<int>()` har sin typ.  Parse-tester 38/38; 148 440 layouter som
clang håller med om.

### Status 2026-11-14 (24): första C++-koden genom vcc:s emitter

`vpp -x c++ -c` ger nu ett objekt.  Medan en funktionskropp tolkas bygger
C++-parsern det delade AST:t med C-front-endens byggare (samma noder,
konverteringar och satser som C ger emittern; `src/frontend/cxx/codegen.c`)
och `cxx_run` lämnar TU:n till `emit_tu`.  Byggt hittills: funktioner i
globala namnrymden över skalära typer (heltal, flyttal, pekare): literaler,
variabler, de inbyggda operatorerna (binära, unära, tilldelning, `++`/`--`),
anrop av sådana funktioner efter överlagringsupplösning, `if`/`while`/
`return`/`break`/`continue`, lokala variabler.  Namnen manglas (C:s
Itanium-manglare för `overloadable`; `main` och `extern "C"` som de är).
En funktion vars kropp har något annat lämnas odefinierad (deklarerad, inte
emitterad): `VCC_CXX_GEN=1` säger var vcc gav upp, och
`VCC_CXX_PROGRESS=1` räknar "function definitions N, with code M".
`bench/cxx-run-tests.py` (`src/test/cxx/run/`): vcc kompilerar, clang++
länkar, programmet ska ge 0, alla funktioner ska ha fått kod och objektets
definierade symboler ska vara de clang++ definierar: 2/2 (rekursion, tre
överlagringar, `bool`, `double`, slingor).
Saknas för json: klasser (medlemmar, `this`, konstruktorer/destruktorer),
referenser, namnrymder och klassnamn i mangling, mallinstansers kroppar,
undantag, vtabeller/RTTI, strängliteraler, `for`/`switch`/`do`.

### Status 2026-11-14 (25): mangling, referenser, struct-värden och medlemsfunktioner i kod

- `mangle.c`: Itanium-mangling för funktioner (namnrymder, klasser,
  mallinstanser med typargument, pekare/referenser/cv, funktions- och
  fälttyper, operatorer, substitutioner, `St`, namnlös namnrymd).
  `bench/cxx-mangle.py` (`src/test/cxx/mangle/`): varje symbol clang++
  definierar ska finnas bland vcc:s namn: 2/2 filer, 34 namn, bland dem
  `std::string`, `vector`, `map`, `unique_ptr` och iteratorer.
- Kod byggs nu också för: `?:`, `static_cast`/C-/funktionscast mellan
  skalärer, `a[i]`, strängliteraler, komma, `for`/`do`, lokala fält med
  platta initierarlistor, variabler i namnrymdsscope; referenser
  (parametrar, lokala, returvärden; `const T&` bundet till ett värde får en
  temporär); klasser som C skickar och kopierar som C++ gör (POD för
  layout): medlemsåtkomst, värdeparametrar, returvärden, tilldelning;
  medlemsfunktioner (i klassen och utanför, `const`, `static`, `this`,
  implicit `this->`, `obj.f()`/`p->f()`), inline-funktioner som svaga
  symboler.
- Medlemsfunktioners kroppar tolkas efter att klassen lagts ut (inte bara
  efter dess sista medlem): `sizeof` av klassen och medlemmar av `*this`
  har sin layout där.
`bench/cxx-run-tests.py`: 4/4 (omkring 70 funktioner, alla med kod).
Saknas för json: konstruktorer/destruktorer och klasser som inte är POD,
`switch`, uppräkningar, mallinstansers funktionskroppar, undantag,
vtabeller/RTTI, lambdas, `new`/`delete`, range-for.

### Status 2026-11-14 (26): konstruktorer/destruktorer, uppräkningar, switch, klassmallars medlemmar i kod

- Uppräkningar (som sina underliggande heltal till emittern, med namn i
  manglingen), `switch`/`case`/`default`, teckenliteraler.
- Konstruktorer (i klassen och utanför) med medlemsinitierare för skalära
  medlemmar; objekt med `T x(args)`, `T x{args}`, `T x = v`, `T x;` anropar
  den konstruktor överlagringsupplösningen valde; destruktorer körs vid
  scopets slut genom emitterns scope-cleanup (i ordning, även vid tidig
  `return`).  C2/D2 har koden, C1/D1 är alias.
- Medlemsfunktioner i klassmallsinstanser får kod där de först används.
  Kodgenereringens tillstånd sparas undan medan en kropp byggs inuti en
  annan; ett anrops val av funktion återställs efter att den anropades
  kropp instansierats (annars anropades fel funktion).
`bench/cxx-run-tests.py` 7/7; `cxx-mangle.py` 3/3 (36 namn).
unit-32bit.cpp med `-c`: 342 av 760 betraktade funktionsdefinitioner har
kod (clang emitterar ca 1 670), 0,17 s.
Saknas för json: funktionsmallars instanser i kod, klasser med baser eller
medlemmar som själva behöver konstruktor/destruktor, värdeöverföring av
klasser med egna specialmedlemmar, temporärer av klasstyp, undantag,
vtabeller/RTTI, lambdas, `new`/`delete`, range-for.

### Status (27): basklasser, konstruktorkedjor, vptr-layout (2026-11-14)
- Basklasser i kod: en konstruktors initierare för baser, delegerande
  konstruktorer och medlemmar av klasstyp anropar den konstruktor som
  överlagringsupplösningen valt; baser och medlemmar som inte nämns
  standardkonstrueras först; en destruktor förstör medlemmar och baser efter
  sin kropp.  Härledd→bas för referenser, pekare och objektet i ett
  medlemsanrop.  `const`-medlemmar initieras; referensmedlemmar byggs inte än.
  Avvikelse att rätta: medlemmar initieras i skriven ordning, inte
  deklarationsordning.
- Layout för klasser med virtuella funktioner (egen vptr i de första 8
  byten, annars primär bas på offset 0): `cxx-layout` 155 339 layouter
  godkända av clang (var 150 420), 0 fel.  Kod byggs ännu inte för sådana
  klassers medlemsfunktioner (inga vtabeller).
- `nullptr`, `reinterpret_cast`, matematik- och strängbuiltins; `static`
  funktioner får intern länkning (`_ZL…`, lokal symbol).
- `VCC_CXX_GEN=2`: innersta uttrycket per funktion som inte fick någon nod.
`cxx-run-tests.py` 12/12.  Alla 71 json-TU:er går igenom `-c` utan fel:
47 006 av 72 378 betraktade funktionsdefinitioner har kod (unit-32bit: 669
av 1 026), 14,5 s väggtid seriellt.  Inget json-test länkar ännu, så det
finns fortfarande ingen mätning mot clang -O2 (243 s CPU).
Kvar för json: vtabeller/RTTI, undantag (emittern saknar C++ EH),
lambdas, temporärer och värdeöverföring av icke-triviala klasser,
implicita specialmedlemmar, standardargument, aggregatinitiering,
`new`/`delete`, range-for, COMDAT, exakt manglade mallinstansnamn,
headerdelning mellan TU:er (vccd).

### Status (28): vtabeller och virtuella anrop (2026-11-14)
- Varje klass håller sin vtabells platser (slutlig överskuggare per plats;
  en virtuell destruktor tar två: D1 och D0).  `_ZTV…` definieras i den TU
  som definierar nyckelfunktionen, annars svagt i varje TU som nämner den
  (mallinstanser alltid svagt).  Konstruktorer och destruktorer sätter
  vptr; anrop går genom vtabellen utom när namnet är kvalificerat
  (`Base::f()`); D0 = D1 + `operator delete`; rent virtuella platser pekar
  på `__cxa_pure_virtual`.
- Implicit standardkonstruktor byggs på plats (baser, medlemmar, vptr);
  en klass utan egen destruktor använder basens när bara den behövs.
- Rättade tysta fel: `Base::f(x);` som sats tolkades som deklaration och
  föll bort; en bas konstruktor kunde väljas för den härledda klassen;
  klasser med medlemsinitierare (`int x = 0;`) får nu ingen konstruktorkod
  alls tills de byggs (förut hoppades initierarna över).
- Begränsningar: bara en bas med vtabell (inga thunks), typinfo-platsen
  är null (ingen RTTI än), virtuella baser läggs inte ut.
`cxx-run-tests.py` 13/13 (ny `virtual.cpp`), `cxx-layout` 158 106 layouter,
0 fel.  Alla 71 json-TU:er går igenom `-c`: 54 507 av 81 500 betraktade
funktionsdefinitioner har kod.  Inget json-test länkar ännu.

### Status (29): klasser som värden (2026-11-14)
- En klass som inte är trivial för anrop (Itanium: egen kopierings-/
  flyttkonstruktor eller destruktor, vptr) returneras i ett objekt vars
  adress kommer först och skickas som adressen till en temporär; skrivet
  explicit för x86-64 (rdi/rax), andra mål får ingen kod för sådana
  funktioner än.  Triviala klasser går som C, men bara om C:s bild av
  posten klassas rätt (klasser med basbyte ≤ 16 byte byggs inte).
- Prvalues av klasstyp är temporärer i blocket som byggs; den som tar
  värdet (`T x = f();`, `return f();`, `T(f())`) riktar om konstruktionen
  till sitt eget objekt (kopieringselision).  `T(args)`/`T{args}` bygger en
  temporär; `return x;` flyttar från en lokal variabel.
- Temporärer med destruktor förstörs vid blockets slut (senare än
  standarden: slutet av fulluttrycket).  Ingen kod där en temporär skulle
  skapas villkorligt eller flera gånger per blockinträde (loopvillkor och
  -steg, `?:`, högerled av `&&`/`||`), eller före en `case`-etikett.
- Inbyggda operatorer på klassoperander ger ingen nod (förut C-fel);
  tilldelning av klass bara när den är en bytekopia.
- Sparar konstruktorns initierarlista när en annan kropp byggs inuti.
`cxx-run-tests.py` 14/14 (ny `class-values.cpp`: clangs antal kopior och
flyttar).  69 json-testfiler går igenom `-c` utan fel: 63 608 av 92 500
betraktade funktionsdefinitioner har kod.  Inget json-test länkar ännu.

### Status (30): rätt mått och delade headers (2026-11-14)
- Nytt mått, `bench/cxx-coverage.py`: de funktioner clang -O0 definierar
  för ett json-test (allt testet använder, mallinstanser inräknade) mot dem
  vcc definierar, efter avmangling utan libc++:s `[abi:…]`-taggar.
  Hela sviten: **146 663 funktioner behövs, vcc har 9 023 (6,2 %)**; vcc:s
  övriga 55 966 är inline-funktioner i headers som inget test använder.
  "N av M funktionsdefinitioner har kod" i status 22-29 mätte alltså fel
  mängd.  unit-32bit: 115 av 1 664.
- Orsaker, i ordning: testkropparna (doctest) får ingen kod (lambdas,
  undantag), så det mesta under dem byggs aldrig; 16 % av anropen saknar
  typ (153 773 anrop, 129 368 typade i en sammanslagen TU), så deras
  mallkroppar instansieras inte ens; manglingen saknar `abi_tag` och skiljer
  inte wchar_t/char16_t/char32_t från heltalstyperna.
  Framändens tider (13,7 s CPU för `-fsyntax-only`) är därmed en
  underskattning: den gör ännu inte allt arbete clang gör.
- Delade headers, övre gräns: alla 69 testfiler i en TU ger 1,47 s vägg
  (1,30 s user) för `-fsyntax-only` och 1,54 s för `-c`, mot 13,7 s som
  separata processer.  Siffran är optimistisk: några tester definierar om
  makron före json.hpp (`#define private public`), så clang avvisar samma
  sammanslagna fil, och vcc kontrollerar inte åtkomst.
- Slutsats hittills: 30x (8,1 s) nås inte med en process per TU ens för
  framänden ensam; med delade headers finns utrymmet, men ingen mätning
  hela vägen finns förrän testerna länkar.

### Status (31): överlagrade operatorer, konverteringsfunktioner (2026-11-14)
- Överlagrade operatorer får kod: `a + b`, `a == b`, `a[i]`, `a(x)`,
  unära, `a = b`/`a += b` (medlem eller på namnrymdsnivå, mallar också).
- Konverteringsfunktioner har namn i klassen (`operator #<typ>`), manglas
  `cv <typ>` och används för klass → skalär och för villkor, `!x`,
  `x && y` (`operator bool`).
- Rättade tysta fel (alla hittade av nya tester):
  - en sats utan nod föll bort tyst om satsen före i samma block hade
    lyckats (`sdone` nollställdes inte) — funktioner kunde alltså sakna
    satser; nu får de ingen kod alls;
  - `call_picked` från ett tidigare anrop kunde tas för operatorns funktion;
  - referenser till inline-funktioner utan kod var svaga, så länkningen
    gick igenom och programmet anropade adress noll — nu är bara
    definitionen svag;
  - en konstruktor hittad via konvertering av argumentet användes för
    kopiering; C-fel vid bindning av klass till skalär eller tvärtom.
- Instansers parametrar: parserns parameterposter kopieras innan typerna
  prövas (att fullborda en klass skrev över dem).
`cxx-run-tests.py` 15/15 (ny `operators.cpp`); `cxx-coverage.py`: 9 315 av
146 663 behövda funktioner (6,4 %).  Doctest-kropparna får fortfarande
ingen kod: `if (const Subcase& s = Subcase(...))`, `try`/`catch`/`throw`,
lambdas, standardargument.

### Status (32): standardargument, deklarationer som villkor (2026-11-14)
- Standardargument: deras token sparas vid deklarationen och tolkas på
  nytt där ett anrop utelämnar dem (funktioner, medlemmar, konstruktorer,
  klassmallsinstansers medlemmar; inte funktionsmallars instanser än).
- `if (T x = e)` / `if (const T& x = e)` och `switch` tolkas som riktiga
  deklarationer; variabeln och villkorets temporärer lever i ett block runt
  satsen (rätt livstid för doctests `Subcase`).
- Delsatser (grenar, loopkroppar, etikettsatser) får ett eget block för
  sina temporärer: förut hamnade `if (c) use(make());`:s temporär i det
  omgivande blocket och förstördes även när den aldrig skapats.
`cxx-run-tests.py` 17/17 (nya `default-args.cpp`, `conditions.cpp`);
`cxx-coverage.py` 9 344 av 146 663 (6,4 %).  Kvar i doctest-kropparna:
`try`/`catch`/`throw` (emittern saknar LSDA/landningsplattor), lambdas.

### Status (33): lambdas, `auto`-lokaler (2026-11-14)
- Ett lambdauttryck (inte generiskt, utanför mallar) får kod: kroppen
  tolkas först för typerna och sedan en gång till som slutningstypens
  `operator()` (en lokal symbol `__vcc_lambda_N`).  Variabler i omgivande
  funktioner blir fångster när kroppen nämner dem (`[&]`, `[=]`, `[x]`,
  `[&x]`, `[this]`); slutningstypen får en medlem per fångst, och
  slutningsobjektet är en temporär där uttrycket står.  Nästlade lambdas
  fångar genom varandra.  Inte än: init-fångster, `*this`, kopiering av
  klasser med egen kopieringskonstruktor, konvertering till funktionspekare.
- Slutningstyper heter `$_N_<hash av TU:n>` i manglade namn, så att mallar
  instansierade med dem i två TU:er inte slås ihop.
- `auto x = e;` som lokal variabel får sin symbol när typen är härledd.
`cxx-run-tests.py` 18/18 (ny `lambdas.cpp`); `cxx-coverage.py` 9 430 av
146 663 (6,4 %).  Kvar i doctest-kropparna: `try`/`catch`/`throw`.

### Status (34): undantag och RTTI (2026-11-14)
- Assemblern (`vas`): `.cfi_personality` och `.cfi_lsda` (zPLR i
  `.eh_frame`), byte för byte som clang (`vas-obj` 65/65).
- `throw x`: `__cxa_allocate_exception`, objektet konstrueras på plats,
  `__cxa_throw` med typinfo och destruktor; `throw;` är `__cxa_rethrow`.
- RTTI: `_ZTI`/`_ZTS` för klasser definieras svagt vid TU:ns slut
  (`__class_type_info`, `__si_class_type_info`, `__vmi_class_type_info`;
  nyckelfunktionsregeln som för vtabeller), och vtabellernas typinfo-plats
  är nu ifylld.  Alla baser antas vara publika.
- `try`/`catch` utan stöd i emittern: try-kroppen byggs som en egen
  funktion (en slutning som fångar allt per referens) och anropas genom en
  liten funktion skriven i assembler per try-block, som har
  undantagstabellen (LSDA) och returnerar vilken hanterare som tog
  undantaget.  Hanterarna ligger kvar i den omgivande funktionen;
  `__cxa_end_catch` körs som blockets städning.  Bara x86-64.
- Begränsningar: `return`/`break`/`continue` ut ur en try-kropp ger ingen
  kod än; en hanterare som lämnas via ett undantag (`throw;`) avslutar inte
  sin catch (objektet läcker och ligger kvar på runtimens stack — måste
  rättas innan doctest fungerar); lokala objekt förstörs inte när ett
  undantag passerar en funktion (inga städplattor).
`cxx-run-tests.py` 19/19 (ny `exceptions.cpp`); `cxx-coverage.py` 9 430 av
146 663 (6,4 %).

### Status (35): referensmedlemmar, implicita destruktorer (2026-11-14)
- Referensmedlemmar: en pekare i objektet, bunden av konstruktorns
  initierare, avrefererad vid åtkomst.
- Implicita destruktorer byggs som riktiga funktioner när en klass utan
  egen destruktor har medlemmar eller flera baser som behöver förstöras;
  `~T() = default` får sin kropp där den först används.
- `= nullptr` som standardvärde för en mallparameter är nu ett känt värde
  (`enable_if<...>::type* = nullptr`): doctests `operator<<` väljs, och
  fler mallar instansieras.  `const T` med T en referenstyp tappar `const`.
- `if (T x __attribute__((unused)) = e)` känns igen som deklaration.
- `VCC_CXX_GEN=3`: var `cxx_gen_call` gav upp om ett anrop.
- Layoutkontrollen hoppar över klasser nästlade i en icke-publik del
  (clang kan inte namnge dem): 157 172 layouter, 0 fel.
`cxx-run-tests.py` 20/20 (ny `members.cpp`); `cxx-coverage.py` 10 243 av
146 663 (7,0 %).  Ett minimalt doctest-test kommer nu förbi alla
doctest-makron men stannar i libc++: `std::string`s konstruktorer
(`[[no_unique_address]]`-medlemmar i initierare, `__min_cap` utan typ),
och testregistreringen kräver dynamisk initiering av globala variabler
(`.init_array`), som inte finns än.

### Status (36): dynamisk initiering av globala variabler (2026-11-14)
- En variabel på namnrymdsnivå vars initierare inte är en konstant
  initieras vid programstart: initieraren byggs i en egen funktion
  (`__vcc_init_N`, emitterns `constructor`-attribut → `.init_array`), i
  definitionernas ordning; konstanta initierare ligger kvar i objektfilen.
- Globala objekt av klasstyp konstrueras där (`T x;`, `T x(a)`, `T x{a}`,
  `T x = e`), och deras destruktor registreras med `__cxa_atexit`.
  Förut nollades `T x;` tyst utan konstruktoranrop.
- Bara variabler i den globala namnrymden än; statiska lokala variabler
  (med vakter) saknas.
`cxx-run-tests.py` 21/21 (ny `globals.cpp`: ordning, konstruktion och
destruktion i omvänd ordning vid exit); `cxx-coverage.py` 10 247 av
146 663 (7,0 %).

### Status (37): mot en länkande `std::string` (2026-11-14)
Mål för stunden: ett program med tre funktioner som bara konstruerar
`std::string` ska länka och köra.  Odefinierade symboler: 27 → 9.
- Onamngivna uppräkningar (`enum { cap = ... };`) ger sina uppräknare typ
  och värde (förut otypade: `std::string::__min_cap`).
- Tomma `[[no_unique_address]]`-medlemmar konstrueras i initierarlistor
  (på objektets egen adress).
- `T() = default` utförs på plats (baser, medlemmar, vptr; ingenting för en
  union); tilldelning utan deklarerad `operator=` är en bytekopia när
  klassen tillåter det.
- Standardargument för funktionsmallars instanser (sparas när instansens
  signatur tolkas); kopieringsinitiering från en array (`T x = "text"`)
  ser en pekare.
- `if constexpr`: bara den tagna grenen blir kod; fel i oevaluerad kod
  fäller inte längre funktionen.
Kvar för `std::string`-programmet: `basic_string()` och
`basic_string(const char*, const allocator&)` (kroppar utan kod),
konstanter på namnrymdsnivå (`__not_found`, `__ambiguous`: måste få en
definition eller vikas), `__fill_n`, och de breda `__constexpr_w*`.
`cxx-run-tests.py` 21/21; `cxx-coverage.py` 10 403 av 146 663 (7,1 %).

### Status (38): ett `std::string`-program länkar och kör (2026-11-14)
`std::string s("xy"); s.size()` och `std::string s; s.size()` kompileras
av vcc, länkas mot libc++ och ger clangs värden (2 och 0).
- Inline-funktioner (och vtabeller, typinfo) är `SF_LINKONCE`: de emitteras
  bara när något med kod använder dem, som clang -O0.  Ett alias
  `C1 = C2` till en konstruktor som namngavs innan den var känd som inline
  pekar inte på en bortrensad funktion.
- Symboler med samma assemblernamn delas (`cxx_gen_symbol_named`); medlemmar
  i anonyma structar får sina standardinitierare; heltalskonstanter på
  namnrymdsnivå viks.
- `cxx_ctor_use` behåller sitt konstruktorval när destruktorns kropp
  instansieras (den kroppen konstruerar egna objekt och skrev över valet:
  `basic_string(const char*)` blev `__rep`s konstruktor).
- `m()` och `T()` värdeinitierar: utan användarskriven standardkonstruktor
  nollas objektet först (`__rep_()` med `__rep() = default`), sedan
  konstrueras det om något behöver det.
`cxx-run-tests.py` 22/22 (ny `value-init.cpp`).  `cxx-coverage.py`: 0 av
71 TU:er fallerar, 2 138 av 146 663; talet sjönk från 10 403 eftersom
oanvända inline-funktioner inte längre emitteras (en funktion utan kod drar
inte in sina anrop), inte för att mindre kod genereras.  Kvar: strängliteral
som variadiskt argument (`printf("%lu", ...)` blir otypad), rethrow ur en
hanterare, statiska lokala variabler, implicita kopieringskonstruktorer.

### Status (39): första mätningen hela vägen, med clang som utfyllnad (2026-11-14)
`bench/cxx-weakfill.py`: varje json-test kompileras av vcc -O0; clangs -O0-objekt
för samma TU görs svagt (`llvm-objcopy --globalize-symbol=_Z* --weaken`) och
länkas med, så att det vcc inte gav kod kommer från clang.  Namn som ingen
definierar (vcc manglar annorlunda) blir stubbar som fäller (`ud2`).
- **66 av 70 test länkar och går igenom** (4 anropar en stubb:
  32bit, bjdata, deserialization, regression2).
- vcc -O0, TU för TU: **18,6 s CPU** (3,2 s vägg, -j 6), 13x mot clang -O2:s
  243 s.  -fsyntax-only 18,1 s; kodgenereringen lägger till 1,2 s, men vcc
  emitterar bara 2 038 av clangs 145 428 funktioner (199 kB av 22 MB .text).
- Headrarna ensamma (doctest + json.hpp) tar 0,17 s per TU: 11,9 s av 18,1 s
  är samma headrar 70 gånger.
- Assemblern var kvadratisk i antal funktioner (`as_fragment_latest` gick hela
  kedjan av fortsättningsfragment vid varje byte till `.text`): 145 000
  C-funktioner tog 100 s, nu 2,2 s (clang -O0 10,7 s).  Utdata identisk.
Prognos (uppskattning, inte mätning): med förkompilerade headrar 0,17 s +
TU-eget arbete 7,4 s + resten av kodgenereringen (~143 000 funktioner à
~15 µs, 2,1 s) ≈ 9,7 s, **25x**.  Delas även instansieringarna (som i
unity-bygget, 1,5 s för alla -c) blir det ~3,7 s, ~65x.  Framändan hoppar ännu
över arbete (otypade anrop, ej instansierade kroppar), så siffrorna är
optimistiska.  30x kräver alltså delade headrar och troligen delade
instansieringar (vccd), inte bara PCH.

### Status (40): headrarna en gång — 66x mätt, med clang som utfyllnad (2026-11-14)
`bench/cxx-fork.py`: en forkserver i stället för förkompilerade headrar.  vpp -c
av en prefixfil (testets `#define JSON_*/DOCTEST_*`, `doctest_compatibility.h`,
`nlohmann/json.hpp`, `#pragma vcc_fork`) tolkar headrarna en gång och forkar
sedan ett barn per TU som fortsätter med TU:ns egen text och skriver sitt eget
objekt (`cxx_fork_tus` i translation-unit.c; job.c tar objektets sökväg).
Varje TU kompileras fortfarande för sig.  70 TU:er, 3 prefix (68, 1, 1).
- **vcc -O0: 3,7 s CPU för hela sviten, 66x mot clang -O2:s 243 s**
  (TU för TU 17,9 s, 13,6x).  69 av 70 test går igenom på båda sätten
  (unit-32bit fallerar i båda).
- Objekten är desamma: alla 70 har samma funktioner och samma .text
  (199 360 byte) som när TU:erna kompileras var för sig.
- Förbehåll: vcc genererar fortfarande bara ~1 % av koden (clangs svaga
  objekt fyller resten).  Kodgenereringen för resten skattas till ~2 s
  (C-emittern: 145 000 funktioner på 2,2 s), framändan hoppar ännu över
  arbete (otypade anrop, ej instansierade kroppar).  En full kodgenerering
  hamnar därför troligen kring 6 s, ~40x: över 30x, men inte mätt.
Unity-bygget (`bench/cxx-unity.py`) är ingen bra ersättare: clangs egna
unity-byggen av de flesta grupperna fallerar sina test (testen påverkar
varandra), så bara 55 giltiga grupper återstår.
Mangling som nu stämmer med clang: abi_tag, bool/unsigned-värden i
mallargument, funktionsmallars deklarerade signatur (T_), wchar_t/char8/16/32.

### Status (41): new/delete, och ett fel som tidigare dolts (2026-11-14)
- `new T(args)`, `new T{...}`, `new T`, `new T[n]` (skalärer), `new (p) T(...)`,
  `delete`, `delete[]` (storleksangiven `_ZdlPvm` som clang 23), `p->~T()`;
  `operator new/delete` som namngivna funktioner (deklareras implicit, manglas
  `nw/na/dl/da`), `::operator delete(p);` tolkas som anrop, inte deklaration.
  Två deklarationer av samma funktion (implicit och `<new>`s) är en kandidat.
- Fel kod som nu syns när fler funktioner får kod: en `const std::string&`
  bunden till en strängliteral fick literalens adress.  Nu en temporär via
  den konverterande konstruktorn (`converting-bind.cpp`).  Hittades med
  `VCC_CXX_NOGEN=fil` (vcc ger inte de namngivna funktionerna kod, clangs
  används): bisektion över TU:ns funktioner.
- Tilldelning av pekare konverteras som i C++ (Derived* till Base*).
`cxx-fork.py`: 69 av 70, 3,8 s CPU (65x); vcc definierar 2 209 funktioner
(från 2 038).  `cxx-run-tests.py` 26/26.  Kvar som känd skillnad:
temporärer förstörs vid blockets slut, inte uttryckets.

### Status (42): fler byggstenar; flaskhalsen är iostreams (2026-11-14)
Nytt: statiska lokala variabler med konstant initierare, lokala fält utan
storlek (`T a[] = {...}`, `char s[] = "..."`), `__builtin_isnan/isinf/
isfinite/isnormal`, `__builtin_LINE/FILE/FUNCTION/...`, `__func__`/
`__PRETTY_FUNCTION__`, `c ? void(0) : f()` (assert), klammerelision för
fältmedlemmar (`std::array<int, 3> a{1, 2, 3}`), typade värdemallparametrar
(`size_t N`), lambdatyper med skalära fångster som POD.
Mätt: bara 10 av json-testens 118 testfunktioner får kod av vcc, och det
ändrades inte av detta: varje testkropp stannar längre ned.  Den vanligaste
blockeringen i testkroppar är doctests `CAPTURE`/`INFO` och utskrifter, som
går genom `std::ostream`: libc++:s `basic_ostream` har en virtuell bas
(`ios_base`), som vcc varken lägger ut eller anropar genom.  Därefter
`<=>`/`std::is_lt`, `operator,` på klasser, `pointer_traits::pointer_to`.
Nästa steg för att vcc själv ska generera json-sviten: virtuella baser
(layout, VTT, konstruktorer), sedan jämförelseoperatorerna.  `cxx-fork.py`
oförändrat: 69/70, 3,8 s CPU, 2 215 funktioner.

### Status (43): referenser till alla klasser; nästa hinder är packar och virtuella baser (2026-11-14)
En referens är en adress som en pekare: `std::ostream &` som parameter
fäller inte längre funktionen (`put(std::ostream &, int)` får kod; vad som
görs genom referensen kontrolleras där det görs).  json-sviten oförändrad
(69/70, 3,8 s, 2 215 funktioner, 10 av 118 testfunktioner): testkropparna
stoppas av allmänna luckor, i ordning efter hur ofta de syns:
- funktionsmallar med parameterpackar (`std::tie(Args&...)`,
  `__construct_at_end`): instansens typ beräknas inte, anropet blir otypat;
- virtuella baser (`basic_ostream`: `operator<<(ostream&, char)` och allt
  som skriver till en ström);
- `operator()` på en temporär (`__destroy_vector(*this)()` i `~vector`);
- `<=>` med `std::is_lt`, `static_cast<const Bas &>(*this)`.
Fel som hittades: medlemsfunktioner definierade utanför en explicit
specialisering med `inline` (`__bitset<0, 0>`) blir starka symboler.

### Status (44): funktionsmallar med parameterpackar får kod (2026-11-14)
En instans av en mall med en funktionsparameterpack (`f(A&&... a)`) får nu
en entitet och kod: en parameter per element, `a...` i argumentlistor
expanderas till en nod per element, `sizeof...(T)` är en konstant, tomma
packar fungerar.  Namnen är clangs (`_Z3fwdIJiRlEEiPFiilEDpOT_`,
`_Z5countIJEEiDpRT_`).  Inte än: fold-uttryck (`(0 + ... + v)`) får varken
typ eller kod.  json-sviten oförändrad (69/70, 3,9 s, 2 215 funktioner,
10 av 118 testfunktioner): libc++:s packanvändare stannar på andra luckor
(virtuella baser, `operator()` på temporärer).

### Status (45): klassmallars medlemmar definierade utanför klassen får kod (2026-11-14)
- En instans medlem definierad utanför klassmallen (`template<class T> int
  L<T>::f() {...}`) tolkades bara för typning; nu får den kod (svag, som
  clangs).  libc++:s `vector`, `__vector_layout`, `basic_stringbuf` ...
- `operator()` på en temporär: argumenten läses före typningen (som kan
  instansiera kroppen och skriva över parserns argumentlista).
- En klass som bara är framåtdeklarerad har sitt scope, så att namn med en
  referens till den kan manglas.
- `mutable`-medlemmar i const-objekt.
`std::vector<int>`-programmet: allt utom `push_back` (en medlemsmall med pack
definierad utanför klassen).  json: 69/70, 3,8 s, 2 230 funktioner,
10 av 118 testfunktioner.

### Status (46): json-funktioner kan namnges (2026-11-14)
- Mallar som mallargument manglas (`basic_json<std::map, std::vector, ...>`):
  varje funktion med `json` i signaturen saknade namn och fick ingen kod.
  Nu samma namn som clang.
- Funktioner i en namnlös namnrymd har intern länkning (som clangs `t`).
- `static_cast<F *>(&f)` väljer överlagringen av f med typen F.
- En instans som bara deklareras här (en medlemsmall definierad utanför
  klassen) har sin typ, så att anrop till den byggs (kroppen: inte än).
- Anrop genom en funktionspekare med en klass som argument: `operator()`
  söks bara för klassobjekt (förut blev anropet otypat).
json: 69/70, 4,1 s CPU (59x), 2 395 funktioner (från 2 230), 10 av 118
testfunktioner.  Nästa stopp i testkroppar: typdrag som
`__is_nothrow_constructible` på klasser (noexcept på konstruktorer följs
inte), `<=>`, virtuella baser.

### Status (47): noexcept följs; nothrow-typdrag för klasser (2026-11-14)
`noexcept`, `noexcept(e)` (konstant), `throw()` registreras på funktioner och
konstruktorer.  `__is_nothrow_constructible` och vänner svarar för klasser:
den valda konstruktorns noexcept, och för implicita och `= default`
konstruktorer: inget kastar om basernas och medlemmarnas motsvarande
konstruktorer inte gör det (rekursivt; `std::runtime_error` som medlem).
Kontrollerat mot clang med static_assert (`nothrow-traits.cpp`).
json: 69/70, 4,2 s CPU, 2 406 funktioner, 11 av 118 testfunktioner
(unit-noexcept:s testfunktion får nu vcc:s kod).

### Status (48): `<=>` och `<compare>` genom vcc enbart (2026-11-14)
Inbyggt `<=>` på skalärer typas `std::strong_ordering`/`partial_ordering`
och byggs som ett objekt av den klassen (libc++:s: en `signed char`, -127
för oordnat; förut ett hårt fel som fällde hela TU:n).  Vänfunktioner
definierade i en klass får kod (de tillhör den omgivande namnrymden och är
inline); en klass till en annan som värde genom konverteringsfunktion eller
konverterande konstruktor (`0` till `_CmpUnspecifiedParam`); statiska
medlemmar av klasstyp definierade efter klassen (`partial_ordering::less`,
även när `T X::m(e)` tolkas som en funktionsdeklarator) som temporärer från
sin initierare; `?:` av en klass.  Ett program med `std::is_lt(a <=> b)`,
`std::is_gt`, `partial_ordering::unordered` kompileras, länkas och körs
med enbart vcc:s kod.  json oförändrad: 69/70, 2 406 funktioner, 11 av 118.
Rättelse till (48): commit 67d015e4 sade att `partial_ordering::unordered`-
jämförelsen kördes med enbart vcc:s kod; den länkade inte då (`operator==
= default` fick ingen kod).  Nu: en förvald `operator==` för en klass med
skalära medlemmar byggs på plats (medlemmarna jämförs i ordning), och hela
testet kompileras, länkas och körs med enbart vcc:s kod.

### Status (49): medlemsmallar definierade utanför klassmallen får kod (2026-11-14)
`template<class T> template<class... A> R X<T>::f(A&&...)`: definitionen
registreras (båda parameterlistorna) och tolkas för en instans vars klass bara
deklarerar medlemsmallen: klassinstansens argument för de yttre parametrarna,
instansens för de egna, klassens medlemmar synliga vid namn.  Med och utan
pack (`member-templates.cpp`).  `vector::emplace_back` stannar nu på en pack
som används i en lambda (`[&] { f(std::forward<A>(a)...); }`).
json oförändrad: 69/70, 2 406 funktioner, 11 av 118.

### Status (50): packelement fångade av lambdor (2026-11-14)
Ett packelement fångat med `[&]` byggs i closure-objektet från sin egen
symbol (förut från packentiteten, som då inte längre pekade på elementet).
En medlemsmall definierad utanför klassen vars omtolkning misslyckas behåller
sin deklarerade typ.  `vector::emplace_back`: `__emplace_back_slow_path`
saknar fortfarande typ i lambdans kropp (troligen instansen som lambdans
första, typande pass sparade).  json oförändrad: 69/70, 2 406 funktioner.

### Status (51): ett tyst tappat anrop, fler kandidater rätt, egna namn spärrade (2026-11-14)
- `X<T>::f(a, b);` i ett block tolkades (tentativt) som en deklaration och
  försvann tyst: funktionen fick kod utan anropet (fel kod).  Ett block
  deklarerar inget kvalificerat namn; nu är det ett uttryck.
- En mall med färre parametrar än anropets argument (och utan pack) är ingen
  kandidat (`min(initializer_list<T>)` bredvid `min(a, b)`).
- `__builtin_assume_aligned`; en packinstans tagen ur cachen anger sin
  funktion för anropet (förut en inre instans som låg kvar).
- När vcc inte kan skriva clangs namn (en deklarerad signatur med beroende
  typer som `__type_identity_t<T>`, värdeparametrar som `enable_if_t<...>`,
  där clang 23 skriver `Tn <typ>`) är namnet vcc:s eget; ett anrop till en
  sådan funktion utan vcc:s kod byggs inte (inget skulle definiera namnet).
json: vcc definierar **4 192 funktioner (från 2 406)**, 69/70, 4,7 s CPU,
11 av 118 testfunktioner.  Kvar: packar som inte står sist i mallens
parameterlista (`template<class T, class... A, enable_if_t<...> = 0>`),
manglingen av beroende typer, virtuella baser.

### Status (52): packar som inte står sist (2026-11-14)
`template <class T, class... A, enable_if_t<cond, int> = 0>`: en pack följd
bara av parametrar med förval deduceras; förvalen räknas med packen bunden,
och en värdeparameter vars typ inte bildas (`enable_if<false>::type`) gör
kandidaten ogiltig.  Instansen binder parametrarna efter index.
`allocator_traits::construct` och `std::min<T>`/`max<T>` fungerar nu med
enbart vcc:s kod (program kompileras, länkas, körs).
json: 69/70, 4,8 s CPU, men vcc definierar **2 472 funktioner (från 4 192)**:
fler anrop väljer nu rätt mall, vars kropp ofta inte byggs (t.ex. `std::map`:s
`__tree`-konstruktor) och vars namn är vcc:s eget (clang 23:s `Tn`), så
anroparna får ingen kod.  Inget fel blir kod; täckningen sjunker tills
kropparna och manglingen kommer ikapp.

### Status (53): std::map genom vcc enbart (2026-11-14)
`const_cast` byggs (förut gav det ingen kod alls), referenscasts jämför
klassen (samma post oavsett hur typen stavas), `return f();` i en
void-funktion.  Ett program med `std::map<int, int>` kompileras, länkas och
körs med enbart vcc:s kod.  json: 69/70, 2 484 funktioner, 11 av 118.

### Status (54): virtuell destruktor ensam, using-deklarationer, inline utanför klassen (2026-11-14)
- En klass vars enda virtuella funktion är destruktorn (`std::ios_base`)
  fick ingen vtable-post: destruktorn registreras nu, så klassens medlemmar
  kan namnges och anropas (`ios_base::setf`).
- En using-deklaration (`using ::memcpy;` i `std`) kopierar namnets
  hemvist och länkning; `std::memcpy` kan anropas.
- `inline X::f() {...}` utanför klassen är svag (som clangs), emitteras där
  den används; förut stark (två TU:er hade krockat).
json: 69/70, 4,9 s CPU, 2 204 funktioner (färre: de felaktigt starka
inline-definitionerna utan användning försvinner), 11 av 118.

### Status (55): förval före en pack (2026-11-14)
`template <class O = std::string, class... A> O concat(A&&...)` (json:s
`concat`, i alla felmeddelanden): en parameter före packen med förval tar
förvalet när den varken deduceras eller ges.  json: 69/70, 5,1 s CPU,
2 255 funktioner, 11 av 118.

### Status (56): omskrivna kandidater för != (2026-11-14)
`a != b` med bara `operator==` deklarerad blir `!(a == b)` (C++20), medlem eller vän.
cxx-run får en rad `// std:` per test (rewritten-ne.cpp, c++20); 40/40.  cxx-fork: 4,9 s CPU,
49x mot clang -O2, 69/70 test går igenom; vcc definierar 2 255 funktioner.  Målet (30x med bara
vccs kod) är inte uppfyllt: det mesta av json-koden kommer fortfarande från clang.

### Status (57): variabler i namnrymder och statiska datamedlemmar (2026-11-14)
Fel kod rättad: `n::x` fick symbolen `x` (krock med varje annan `x`); nu `_ZN1n1xE`, med `L` för
intern länkning (static, const) utom i en namnlös namnrymd (`_ZN12_GLOBAL__N_11vE`, också för
funktioner).  Statiska datamedlemmar i vanliga klasser har en egen variabel (`_ZN1D3gotE`),
definierad av `T X::m = ...;`; en konstant med initierare i klassen används som värde.  Inline-
variabler är svaga; explicita specialiseringar av variabelmallar får ingen symbol än.  constexpr
som inte är heltal (double) får ett objekt.  `T()` för pekare och flyttal är 0, std::nullptr_t är
en void * i genererad kod.  cxx-run 44/44; cxx-fork 69/70, 5,1 s CPU (47,5x), 2 556 funktioner
från vcc.  Kvar här: index i en arrayvariabel i en namnrymd, variabelmallar.

### Status (58): Tn för värdeparametrar av beroende typ (2026-11-14)
clang 23 skriver `Tn <typ>` före ett värdeargument vars parameter har beroende typ
(`enable_if_t<villkor, int> = 0`).  mangle.c läser nu parameterns tokens: `[std::]enable_if_t`,
`__enable_if_t` och `typename X<E, T>::type`, villkoret som uttryck (`sr`-namn med nivåer, `nt`,
`aa`, `oo`, jämförelser, `sizeof`, heltal); mallparametrarna blir `T_`, `T0_` med clangs
substitutioner.  Parametrar efter en pack står i deklarationens ordning (`J ... E` först).  En
funktionsmall med `decltype` i deklarationen räknas som gissat namn (clang skriver uttrycket, `DT`).
cxx-run 45/45 (nytt: varje svag definition vcc ger har ett namn clang också definierar);
cxx-fork 69/70, 5,1 s CPU, 2 553 funktioner.  json:s `concat_into` stannar i stället på typningen:
`detect_string_can_append<...>::value` (is_detected över decltype) ger inget svar.

### Status (59): std::forward/move som inbyggda, Tn i medlemsmallar (2026-11-14)
`std::forward`, `std::move`, `std::move_if_noexcept` och `std::as_const` av ett lvärde ger objektet
självt, som clangs inbyggda (inget anrop, ingen funktion; tidigare ett gissat namn för varje
instans).  Tn: en klassmalls parameter skrivs som sitt argument, en okvalificerad variabelmall utan
`sr`, och ingen Tn när villkoret bara nämner klassens parametrar (`C2ILi0EE`).  En destruktor som
körs implicit begär sin kropp (som ett anrop gör).  Ett C1/D1 som ingen definierar men vars C2/D2
finns blir alias vid TU:ns slut.  Gissade namn i unit-testsuites 249 -> 221.  cxx-run 49/49;
cxx-fork 68/70: unit-regression1 anropar `data::~data` för `basic_json<..., long double>`, vars
kropp vcc aldrig tolkar (den nästlade klassens medlemmar i den instansen får inga sena kroppar;
syns som en stubbe, i ett bygge med bara vcc ett länkfel).  Kvar att utreda.

### Status (60): destruktorer som körs implicit får sina kroppar
När en destruktors kropp begärs begärs först kropparna för de destruktorer dess slut kör (medlemmars
och basers klasser): de tolkas före, inte inifrån dess kodgenerering (som bröt den yttre).
`basic_json<..., long double>::data::~data` byggs nu; long double (x87, binary128) i genererad kod.  cxx-run 51/51; cxx-fork 69/70, 5,2 s CPU
(46,7x), 2 666 funktioner från vcc (från 2 551).

### Status (61): decltype-returtyper (DT), json:s egen enable_if_t, T::namn (2026-11-14)
En funktionsmall med efterställd `-> decltype(e)` får clangs namn: `DT <uttryck> E` ur tokens (komma
`cm`, anrop `cl ... E`, funktionsparametrar `fp_`, `fp0_`, en namnrymds variabel `L_Z...E`, olösta
namn `sr`, `T()` som `cv<typ>_E`).  Tn: ett biblioteks eget alias för enable_if bakom namnrymder
(`nlohmann::detail::enable_if_t`), `T::namn` som `N T_ <namn> E` (en egen kandidat), och en
konstruktormalls prefix (`...C2`) är en substitutionskandidat.  Ett anropat objekt av tom klass utan
nod (json:s `::nlohmann::to_json`, en constexpr-referens till ett funktionsobjekt) blir ett temporärt
objekt av klassen.  Gissade namn i unit-testsuites 221 -> 174; json:s konstruktor `basic_json(T&&)`
och `adl_serializer::to_json` har clangs namn.  cxx-run 52/52; cxx-fork 69/70, 2 725 funktioner.

### Status (62): beroende medlemstyper i signaturer, konverterande tilldelning (2026-11-14)
En funktionsmall vars signatur inte går att tolka med platshållare (`typename J::int_t` som
parametertyp: json:s `external_constructor<...>::construct`) manglas ur sina tokens: returtyp och
parametrar, `[const] T [const] [&|&&]`, `T::namn` som `N T_ <namn> E`; går det inte blir namnet en
gissning som förut.  `x = v` för en trivialt kopierbar klass (en union) med konverterande
konstruktorer: den matchande konstruktorns temporära, kopierad (json:s `m_value = val`).
unit-32bit misslyckas likadant med clangs eget bygge (8 påståenden på en 64-bitarsvärd): inget vcc-fel.
Gissade namn i unit-testsuites 174 -> 163; cxx-run 54/54; cxx-fork 69/70, 2 728 funktioner.

### Status (63): polymorfa klasser utan egen destruktor, virtuell delete (2026-11-14)
En polymorf klass utan egen destruktor vars medlemmar ska förstöras (json:s `parse_error`: en
`std::exception` med en `std::runtime_error`) får sin implicita destruktor gjord när vtabellen
behövs; den ersätter basens i vtabellen, så klassen kan returneras som värde.  `delete p` av ett
polymorft objekt anropar den raderande destruktorn (D0) genom vtabellen, inte om p är null.
cxx-run 55/55; cxx-fork 69/70, 2 728 funktioner (json:s create-kedja stannar ännu på annat).

### Status (64): medlemsåtkomst i lambdor, basobjektens C2/D2 (2026-11-14)
Fel: `x.m` där m är en datamedlem byggde först `this->m` när det fanns ett this (en
medlemsfunktion, en lambdas operator()); i en lambda misslyckades det (this fångades inte), så
doctests `INFO` (`mb.m_stream = s` i en lambda) fick ingen kod.  Nu inte efter `.` och `->`.
Ett basobjekt konstrueras och förstörs med basobjektets variant (C2, D2), inte hela objektets
(C1, D1), som Itanium vill (doctests `ContextScopeBase` definierar bara C2).
cxx-run 56/56; cxx-fork 69/70, 3 014 funktioner från vcc (från 2 728).
`X::~X() = default;` utanför klassen (doctests `DOCTEST_DEFINE_INTERFACE`, en nyckelfunktion) får sin
kropp där, inte inline: destruktorerna D0/D1/D2 och vtabellen definieras i TU:n som hos clang.
cxx-run 57/57; cxx-fork 69/70, 3 061 funktioner.

### Status (65): tre fel som gav fel kod, explicita operatoranrop (2026-11-14)
Fel kod rättad: (1) en heltalslitteral utan suffix som inte ryms i int var int, trunkerad
(`0x000FFFFFFFFFFFFF` blev -1, `4294967296` blev 0); nu [lex.icon]:s listor.  (2) ett överlagrat
`operator,` anropades aldrig (den inbyggda kommaoperatorn användes tyst); nu som andra binära
operatorer, den inbyggda om ingen kandidat passar.  (3) `const T&` från en `const char[N]` gav
`T = const char[N]` (fel namn, `IA3_KcE`); en arrays kvalificerare är elementens, T är `char[N]`.
`p->operator+(x)`, `this->operator,(in)`: objektet följer med till anropet.  Det här fick doctests
CAPTURE (`mb * "x := ", x` i en lambda) att få kod, vilket visade litteralfelet i unit-to_chars.
cxx-run 60/60; cxx-fork 69/70, 3 205 funktioner från vcc (från 3 061).

### Status (66): range-for över arrayer och klammerlistor (2026-11-14)
Range-for hade ingen kodgenerering: kroppen tolkades med kodgenereringen på men utan någon slinga
runt (troligen fel kod, kroppen en gång).  Nu: en array (lvärde) och en klammerlista (elementen i en
lokal array av deras typ, `auto` härledd från den, const som initializer_list:s element) blir
`{ E *p = r; for (long i = 0; i < N; i++) { decl = p[i]; body } }`; variabeln som kopia, referens eller
pekare.  En klass med begin/end: ingen kod än (förut kroppen utan slinga).  cxx-run 61/61; cxx-fork
69/70, 3 205 funktioner (json:s range-for går över behållare eller strömmar).

### Status 67 (2026-11-14): range-for över klasser, direktinitiering, requires-mangling

- Range-for över klasser (begin()/end(), medlem eller fri funktion), `T x(e)` för skalärer,
  ett avslutande requires-villkor utvärderas med parametrarna deklarerade, och Q-mangling
  (`Q rq X … R concept`, `dt`/`pt`).  Test: range-for-classes.cpp, requires-params.cpp; 63/63.
- Vad som saknas (VCC_CXX_GEN=2 över sviten, 41 818 misslyckade uttryck): vcc gör 29 av 4 066
  testkroppar.  ~45 % är funktionsparameterpaket (`std::forward<Args>(args)...`), ~10 % medlemmar
  i klassmallar som inte är utlagda, sedan beroende statiska anrop (`traits_type::length`),
  inbyggda funktioner (`__builtin_memmove`, `clzg`) och initierarlistor (`json j = {1, 2}`).

### Status 68 (2026-11-14): std::initializer_list, 128-bitarskonstanter, VCC_CXX_GEN=4

- `T x = {...}` och `T x{...}` med en initializer_list-konstruktor: en lokal array av elementen
  (konverterade, eller konstruerade för en klass och förstörda efter konstruktorn) och en
  initializer_list som pekar på den (libc++:s och libstdc++:s medlemmar).  `json j = {1, 2, 3}` och
  `{{"a", 1}, {"b", {true, nullptr}}}` körs rätt; elementens typer tas från sema (nullptr: nullptr_t).
- `~(unsigned __int128)0` förblir en konstant genom en cast: `numeric_limits<__int128>::min()`
  (odefinierad i 69 av 70 TU:er) får kod.
- `VCC_CXX_GEN=4`: varje funktion utan kod, med namn och första orsak (orsaken sparas över nästlade
  instanser).  Testkroppar som saknas: 103 av 118; de vanligaste orsakerna är `auto x = ...` med ett
  uttryck som inte byggs, lokala objekt av klasstyp som inte godtas (codegen.c, cxx_gen_local),
  doctest:s `asm("int $3")` och try/catch.
- Test: initializer-lists.cpp; run 64/64.  Fork: 69/70 (unit-32bit faller även med clang), 5,2 s CPU,
  46,5x; vcc definierar 3 368 funktioner (förut 3 205), 15 testkroppar.

### Status 69 (2026-11-14): partiella specialiseringar, const-överlagring, D2 för inline-destruktorer

- En partiell specialisering deklarerad efter att en annan använts gjorde att den första slutade matcha
  (mallens lista av partialer realloc:ades, instanserna höll pekare in i den): `pointer_traits<T*>`
  efter `<ostream>`, så `std::set`, `std::map` m.fl. fick ingen layout i testerna.  Varje partial
  allokeras nu för sig.  Test: partial-later.cpp.
- `f()` och `f() const` slogs ihop som en omdeklaration (samma funktionstyp i AST:n): ett icke-const
  objekt anropade const-versionen.  Test: const-overloads.cpp.
- En komplett objekts destruktion anropar D2 för en inline-destruktor (som clang, som bara definierar
  D2 där), D1 annars.
- Run 66/66, parse 38/38, mangle 36.  Fork: 69/70, 6,7 s CPU (36x mot clang -O2:s 243 s); vcc definierar
  20 918 funktioner (förut 3 528) av clang -O0:s 145 428, 16 av 118 testkroppar.

### Status 70 (2026-11-14): <=>, nästlade listor, felkod i tomma medlemmar och listargument

- `operator<=>` deklareras och anropas under sitt namn (förut namnlöst), och `a < b` (<=, >, >=)
  skrivs om till `(a <=> b) @ 0` eller omvänt (C++20); `std::string() < "a"` m.fl. typas nu.
- `operator()`/`operator[]` anropade med namn i en medlem (`return operator[](key_type(key))`).
- `remove_cv`/`remove_cvref` tar bort kvalifikatorer från en arrays element (`const char[2]` → `char[2]`).
- Ett heltal som inte är konstanten 0 är ingen nollpekarkonstant: `string::append(char)` matchar inte
  längre `append(const char *)`; `__is_constructible(T, nullptr_t)` beräknas.
- En parameter av klasstyp konstrueras från en skalär/array via en konverterande konstruktor
  (`j["b"] = 3`).
- initializer_list: element som själva är klammerlistor (`{{"a", 1}, {"b", {true, nullptr}}}`)
  konstrueras från sina argument; elementtyperna kopieras innan konstruktorns kropp instansieras.
- Felkod rättad: kopiering av en tom klass skrev en byte över en annan medlem ([[no_unique_address]]
  `__value_comp_` över `std::__tree::__size_`: map-iteration kraschade); en klammerlista som argument
  gav sitt sista uttrycks nod (`json({...})` blev 0).  En trivial defaulted kopierings-/flyttkonstruktor
  kopierar byte (som clang, som inte definierar den).
- Test: spaceship, empty-member-copy, nested-lists; run 69/69.  Fork 69/70, 6,3 s CPU (38,5x),
  20 916 funktioner.

### Status 71 (2026-11-14): klammerlistor som argument, returtyper typename X<T>::type, två felkodsrisker

- `f({...})`: en klammerlista som argument konstruerar sin parameter (initializer_list<E>, en klass
  via sin konstruktor; `json({...})`, `v.insert(pos, {1, 2, 3})`, `v.assign({4, 5})`), och rangordnas
  som en lista ([over.ics.list]) i stället för som sitt sista element.
- En klass som skickas i register konverteras från en annan typ (const_iterator från iterator).
- `T&&` binder ett temporärt objekt som en konvertering gör av ett lvalue av en annan typ
  (`push_back("text")` väljer `push_back(json&&)` som clang); av två lika konverteringar är `T&&` bättre
  än `const T&` för ett rvalue.
- Returtypen `typename X<T>::type` manglas som skriven (`N...IT_E...E`, förut `v`): std::distance m.fl.
- Felkod förhindrad: ett anrop som inte byggdes lämnade sitt sista arguments nod i c->enode, så
  `f(x);` kunde bli `x;` (nu nollställs noden i cxx_gen_call); positioner för klammerargument som
  inte konsumerades nollställs.
- Test: braced-args; run 70/70.  Fork 69/70, 6,6 s CPU (37x), 21 069 funktioner, 17 testkroppar.

### Status 72 (2026-11-14): asm, const-medlemmar, nullptr_t-tilldelning

- `asm("text")` / `asm volatile("text" : :)` utan operander blir C-frontens asm-post (doctests
  `DOCTEST_BREAK_INTO_DEBUGGER`: 10 testkroppar); med operander: ingen kod.
- En datamedlem i ett const-objekt är const: `x.__layout_.__begin_ptr()` på en const vector väljer
  const-versionen (som clang); `f(const T*)` är mer specialiserad än `f(T*)`.
- `p = v` med ett std::nullptr_t-objekt (std::__exchange(p, nullptr), always_inline hos clang).
- cxx_gen_chain tar bara uttryckssatser (en deklaration flyttas till blocket): ett N_BLOCK i ett uttryck
  stoppade x86-64-lägret för unit-cbor.
- Test: asm-simple, const-members; run 72/72.  Fork 69/70, 6,6 s CPU (36,6x), 21 603 funktioner, 20 testkroppar.

### Status 73 (2026-11-14): alignas, R(A...)-specialiseringar, std::function

- alignas/aligned på klasser och medlemmar läggs ut som clang (förut: ingen layout alls); en tom klass
  med alignas eller med alignerade tomma baser får den storleken (libc++:s aligned_storage,
  `std::function` 48 byte, linjering 16).
- En partiell specialisering på en funktionstyp med paket, `X<R(A...)>`, matchar (typparet får en
  platshållartyp), och `R(A...)` i en instans expanderar ett bundet typpaket (`__base<R(A...)>`).
- `nullptr` (vcc:s nod: 0 som void *) till en klassparameter: ett std::nullptr_t-objekt
  (`std::function cb = nullptr`, json::parse); `T{}` värdeinitierar i stället för initializer_list-
  konstruktorn (lexerns `string_t token_buffer {}`).
- Felkod undvikt: std::function lades ut med fel storlek när alignas räknades som 1 (kraschade i
  unit-locale-cpp); nu byte för byte som clang.
- Test: alignas-layout; run 73/73.  Fork 69/70, 6,8 s CPU (35,6x), 22 297 funktioner, 22 testkroppar.

### Status 74 (2026-11-14): inline namespaces i överlagringar, inbyggt kommatecken

- En namnrymds funktioner och dess inline-namnrymds funktioner med samma namn är en överlagringsmängd
  (libc++: `std::begin(initializer_list)` i std, `std::begin(C&)` i std::__1): `std::begin(s)` och
  `using std::begin; begin(s)` hittade bara initializer_list-versionen.  Mängden byggs som kopior av
  entiteterna och sparas per namnrymd och namn tills någon av listorna ändras.
- `a, b` med en klassoperand och ingen operator, som vcc kan välja: det inbyggda kommatecknet (json:s
  `decltype(begin(c), end(c))` i container_input_adapter_factory).
- Test: inline-ns-overloads; run 74/74.  Fork 69/70, 7,8 s CPU (31x; mer kod genereras), 22 912 funktioner,
  23 testkroppar.

### Status 75 (2026-11-14): std::pair-retur, `X::template f<T>()`, enable_if-överlagringar

- `X::template f<T>()` med ett känt X är ett anrop, ingen typ (`__check_pair_construction<...>::template
  __is_pair_constructible<U1, U2>()` i pair:s enable_if): `return {true, p};` till std::pair byggs.
- `is_constructible`/`is_convertible` från std::nullptr_t; en skalär medlem från ett nullptr_t-objekt
  (`second(std::forward<U2>(u2))`) och andra klassvärden går genom vcc:s konvertering i stället för C:s
  (som gav ett hårt fel).
- Mangling: `Tn enable_if<cond>` med `T*`, `T&`, `Args...` i villkoret (`PT_`, `DpT0_`, kandidater som
  parametrarna hittar).  Förut fick allocator_traits::construct med och utan allokatorns construct samma
  namn och den först definierade vann (fel kod).
- unit-allocator faller i fork-mätningen: testets `static bool next_construct_fails` finns i både vcc:s och
  clangs objekt och läses sektionsrelativt, så de två halvorna ser var sin kopia (en artefakt av det
  blandade bygget, inte vcc:s kod).
- Test: enable-if-overloads; run 75/75.  Fork 68/70, 7,7 s CPU (31,8x), 23 166 funktioner, 24 testkroppar.

### Status 76 (2026-11-14): implicita kopieringskonstruktorer

- Den implicita (eller defaulted) kopierings-/flyttkonstruktorn för en klass med konstruktorer byggs
  medlem för medlem: baserna från källans basobjekt (C2), vptr, klassmedlemmar med sina konstruktorer
  (flytt om kopian är borttagen), skalärer och arrayer kopieras (const-medlemmar via sin adress).
  `throw ex;` med `const E& ex` (json:s parse_error, ~500 funktioner) får kod.
- Test: implicit-copy; run 76/76.  Fork 68/70, 7,7 s CPU, 23 218 funktioner, 24 testkroppar.

### Status 77 (2026-11-14): klasspaket i medlemsfunktioners parametrar

- `R operator()(As... a)`, `const As&... a`, `As&&... a` i en klassmallsinstans (std::function:s
  operator()): deklaratorn tillämpas på varje element, och i kroppen är `a` ett paket av de parametrarna
  (sparas i den sena kroppens post; en funktionsmallsinstans egna paket expanderas som förut).
- Test: class-pack-params; run 77/77.  Fork 68/70, 7,8 s CPU, 23 240 funktioner.

## Status 78 (2026-11-14)
Täckning: braced default-argument (`= {}`), städfunktion för initializer_list-arrayer av klasser med
destruktor, inbyggd binär operator med klassoperand utan skalärkonvertering blir ogiltig (överlagringen
väljer då operatorn), lokala klassers mangling (`Z <funktion> E namn`) och intern länkning för namn i
anonyma namnrymder/lokala klasser.  nullptr till en klass via konverterande konstruktor (en referens-
parameter, `k(ordered_json(nullptr))`) väljer nu `basic_json(nullptr_t)` i stället för mallkonstruktorn
med `void *` (unit-hash).  Mätt: cxx-run 77/77, cxx-parse 38/38, cxx-mangle 36; cxx-fork 68/70 TU:s
klarar testerna (kvar: unit-32bit, som också fallerar med clang, och unit-allocator, en statisk variabel
dubblerad mellan vcc:s och clangs objekt: en artefakt av weak-fill), 8,2 s CPU, 29,7x mot clang -O2.
vcc genererar 27 av 118 testkroppar och 24 545 funktioner; nästa steg är fler testkroppar.

## Status 79 (2026-11-14)
Argumentgränsen 16 (argumentlistor, braced-listor, initializer_list-element) är nu `CXX_MAX_ARGS` (256)
i parser och kodgenerering; överlagringsrankningen i typing.c har kvar sina 16 (funktioner med fler
parametrar är sällsynta, en lista till en initializer_list-konstruktor är ett argument).
`std::vector<uint8_t> v = {...200 element...}` genereras och ger rätt värde.  Mätt: cxx-run 77/77,
cxx-parse 38/38, cxx-mangle 36; cxx-fork 68/70 (unit-32bit går nu ut på tid i stället för att fallera,
fallerar också med clang; unit-allocator: weak-fill-artefakten), 8,0 s CPU; 30 av 118 testkroppar,
24 835 funktioner.  Nästa: ifstream/stringstream-lokaler (9 kroppar), `R"(...)"_json` (flera).

## Status 80 (2026-11-14)
Rättningar: `static_cast<E>(obj)` genom klassens konverteringsfunktion; överlagring väljer konvertering
till närmare basklass ([over.ics.rank], json:s `priority_tag<N>`), och ett objekt av härledd klass till
en basklassparameter kopierar basdelobjektet; ett const-objekt ser bara const-medlemsmallar; i en
const-medlemsmalls kropp är `this` const (instansieringsvägarna satte inte `this_const`), och `this` typas
`const T *` i en const-medlem.  cxx-fork.py: konfigurationsraderna läses fram till `#include
<nlohmann/json.hpp>` (unit-diagnostics definierar JSON_DIAGNOSTICS efter doctest_compatibility.h och
byggdes tidigare utan den; nu ett eget prefix, därav högre CPU-tid).  Mätt: cxx-run 77/77, cxx-parse 38/38,
cxx-mangle 36; cxx-fork 67/70: unit-32bit (tidsgräns, fallerar också med clang), unit-allocator
(weak-fill-artefakten), unit-ordered_json (nu genereras basic_json::erase, som anropar
ordered_map::erase(const key_type&), vars kropp vcc ännu inte bygger: vector::erase via __wrap_iter,
__unwrap_range, transparent jämförare i ett villkor; mallen erase(KeyType&&) som clang väljer faller på
sitt villkor hos vcc).  10,2 s CPU med fyra prefix; 31 av 118 testkroppar.

## Status 81 (2026-11-14)
`frontend/cxx/codegen.c` (5 800 rader) delad i `frontend/cxx/codegen/`, en fil per ämne: state, convert,
name, expression, lambda, call, builtin, construct, aggregate, constructor, destructor, new, vtable,
statement, global, function (150-680 rader), gemensamt i `codegen/codegen.h`.  Ren flytt: -S-utdata
byte för byte lika på unit-algorithms, unit-hash, unit-diagnostics; varje fil kompilerar ensam
(`VCC_UNITY=`; expression.c fick <ctype.h>, cxx_lookahead_fn delas, typing.c går via
cxx_ftemplate_params).  cxx-run 77/77, cxx-parse 38/38, cxx-mangle 36; cxx-fork 67/70 som förut.

## Status 82 (2026-11-14)
Weak-fill-mätningarna borttagna (bench/cxx-fork.py, cxx-weakfill.py, cxx-unity.py: clangs objekt fyllde
det vcc inte kompilerade, vilket dolde luckorna).  I stället `bench/cxx-strict.py`: varje test-TU
kompileras av vcc ensam, med `VCC_CXX_MISSING=FIL` (vcc skriver varje funktion vars kropp den inte
byggde, med assemblernamn och var den gav upp); en funktion som TU:ns eget objekt ska definiera (clangs
gör det) men vcc:s lämnar odefinierad är en saknad kompilering och TU:n fallerar med dem listade; utan
sådana länkas den med doctests main och körs.  Summeringen räknar de saknade per ställe i codegen.
(Kompilatorns fork-server, VCC_FORK_LIST, finns kvar.)  Första körningen: 0 av 70 TU:s kompletta, 67 med
saknade kompileringar (917 funktioner summerat), 3 länkfel (namn vcc manglar annorlunda).  Orsakerna är
ännu för grova: statement.c:600/362 (en sats utan nod) döljer det inre skälet; det är nästa steg.

## Status 83 (2026-11-14)
Funktionsmallar med härledd returtyp (`auto f(T)`): kroppen tolkas en andra gång när typen är härledd,
nu med kod (den första härledde bara typen; anrop till dem byggdes aldrig).  Mangling: `auto` som `Da`,
`decltype(auto)` som `Dc`, en avslutande returtyp (`auto f(T) -> T`) som den skrivna typen (förut `v`).
Statiska medlemmar anropas på const-objekt (`this->__throw_length_error()` i en const-medlem: regression
från Status 80).  `VCC_CXX_MISSING` anger nu det innersta uttrycket utan nod som orsak.  cxx-strict:
0/70 kompletta, 850 saknade funktioner (förut 917); kvar överst: `__tree_.end()` (statement.c:362),
basklassinitiering via alias (`json_base_class_t(std::move(other))`), CTAD (`pair{a, b}` i
`__unwrap_range`), `__libcpp_is_trivially_relocatable<T>::value`, `decltype(auto)` otypat.

## Status 84 (2026-11-14)
En tilldelning bunden till en referens är vänstersidan (`return l = l | r;` till `E&`: `(p = &l, *p op= r,
p)`, l beräknas en gång); ett klassvärde av annan klass konverteras med målets konverterande konstruktor
när ingen konverteringsfunktion finns (`return __tree_.end();` till `__map_iterator`); ett aggregat vars
element är en braced-lista (`M m{{nullptr, 3}, 4}`, nästlat, `{}`) initierar medlemmen från dess element.
cxx-strict: 686 saknade funktioner (850).  Överst nu: basklass via alias i konstruktorinitierare (87),
CTAD i `__unwrap_range` (79), `__libcpp_is_trivially_relocatable<T>::value` (79), kroppar vcc aldrig
tolkade (67), `filldata<T>::fill` (38).

## Status 85 (2026-11-14)
Konstruktion från ett objekt av härledd klass använder basdelobjektet (`B(o)` i D:s kopieringskonstruktor,
json:s `json_base_class_t(other)`); `__is_trivially_copyable`/`__is_trivially_relocatable`/`__is_trivial`
är falska för en klass med icke-trivial kopierings-/flyttkonstruktor eller destruktor (förut okända:
`__libcpp_is_trivially_relocatable<string>` i vector).  cxx-strict: 541 saknade (686).  Överst: kroppar
vcc aldrig tolkade (87), CTAD i `__unwrap_range` (79), `filldata<T>::fill` (38).

## Status 86 (2026-11-14)
CTAD för deduktionsguider av den enkla formen `Name(T1, const T2 &, T3 &&) -> Name<T...>` (pair{a, b}):
guiderna sparas på klassmallen (förut tolkade och kastade), `Name{args}`/`Name(args)` utan mallargument
deducerar från dem; implicita guider från konstruktorer saknas.  cxx-strict oförändrat 541: de 79
funktionerna i `__unwrap_range` stannar nu vid nästa sak, `__libcpp_is_contiguous_iterator<_Iter>::value`
(86).  Känt: den första konstruktionen av en `pair<A, B>` i en TU misslyckas (anropet byggs innan
konstruktorinstansens typ finns), senare lyckas.

## Status 87 (2026-11-14)
Den första konstruktionen av en klassinstans i en TU (`pair<int, double>(x, 2.5)`) använde en annan
anrops argumenttyper: att komplettera klassen (i cxx_type_class_scope/require_complete) tolkar dess
medlemmar och skriver över c->cargs, som argumenten pekar på; cxx_ctor_use kopierar nu argumenten
först.  cxx-strict 546 (541: fler anropare byggs nu och når nya luckor).

## Status 88 (2026-11-14)
`__is_convertible(A, B)` mellan två klasser är falskt när B inte är A:s bas, B inte deklarerar egna
konstruktorer och A inga konverteringsfunktioner (iteratortaggar: `__libcpp_is_contiguous_iterator` för
vector-iteratorn blir känd).  En use-after-free i `cxx_gen_symbol_named`: en uppslagning växte tabellen
som `cxx_gen_complete_object_aliases` gick igenom (unit-comparison kraschade, hittad med ASan); nu växer
den bara vid insättning.  cxx-strict 550; överst: kroppar vcc aldrig tolkade (86), `_Algorithm()(...)`
i `__copy_move_unwrap_iters` (79), `filldata<T>::fill` (38).

## Status 89 (2026-11-14)
`__is_trivially_assignable`/`__is_nothrow_assignable` för skalärer och för samma klass (POD ja; icke-trivial
kopiering/flytt/destruktor nej).  `sizeof` av en typ som förblir ofullständig (instans av en mall utan
definition och utan matchande partiell specialisering) är ett substitutionsfel: libc++:s
`__has_specialization_v`/`__is_segmented_iterator_v` (vector 0, deque 1, som clang).  CTAD bevarar
argumentlistan medan klassen instansieras (`std::pair{x, 2.5}` får kod, rätt värde).  cxx-strict
oförändrat 550: för `json*` är valet av partiell specialisering av `__segmented_iterator_traits` okänt
(begränsningar över `iterator_traits<json*>`), så `_Algorithm()(...)` (79) står kvar.

## Status 90 (2026-11-14)
Ett substitutionsfel i en atomär begränsning gör den ouppfylld ([temp.constr.atomic]/3): en requires-sats
där `T::x` med T utan klass (member_missing) eller ett annat substitutionsfel (einvalid) inträffar är falsk
när den är en konjunktion (med `||` överst: inte avgjort).  Därmed väljs `__segmented_iterator_traits`
för `json*` (join_views begränsade specialisering faller), och `std::copy`/`std::move` över json-vektorer
får kod.  cxx-strict: 3 av 70 TU:s klarar testerna med bara vcc:s kod, inga länkfel, 473 saknade (550).

## Status 91 (2026-11-14)
En medlemsmall definierad utanför klassmallen hittas för primärmallens instanser även när en partiell
specialisering definierar en med samma namn (`vector<bool, A>::__construct_at_end` bredvid
`vector<T, A>::__construct_at_end`: två träffar gjorde att ingen kropp tolkades).  cxx-strict 465 (473).

## Status 92 (2026-11-14)
`::__builtin_x(...)` är inbyggnaden (ingen deklaration i globala namnrymden).  Partiell ordning med
arrayer vars längd är en platshållare (`F<const char[N]>` mot `F<T[N]>`: den förra mer specialiserad;
förut föll båda och primärmallen valdes, doctests `filldata<const char[N]>`).  cxx-strict 413 (465).

## Status 93 (2026-11-14)
`return "text";` (eller 0, nullptr) från en funktion som returnerar en klass via adress: kopieringsinitiering
i anroparens objekt med en konverterande konstruktor.  cxx-strict 392 (413).

## Status 94 (2026-11-14)
`T x({}, v)`: en braced-lista som argument i direktinitiering blir en temporär av parameterns klass
(listornas positioner skickas till konstruktoranropet; ett anrop utan sådana argument tar dem inte).
cxx-strict 392 (oförändrat: de funktionerna stannar vid nästa sak).

## Status 95 (2026-11-14)
`T(const T&) = default` för en klass som inte är trivialt kopierbar (polymorf: `std::exception`) byggs på
plats: baser, vptr, medlemmar (förut ett anrop till en funktion som aldrig definierades).  cxx-strict 388.

## Status 96 (2026-11-14)
`c ? a : b` av två lvärden av samma klass är ett lvärde (const om någon är det): `*(c ? &a : &b)`
(json_ref::moved_or_copied).  `extern template R X<args>::f(params);` (libc++:s medlemslistor för
basic_string<char>) registreras: en medlem den täcker, och som inte har
`__exclude_from_explicit_instantiation__` (`_LIBCPP_HIDE_FROM_ABI`), får ingen kropp här; anropet går
till biblioteket (basic_string:s kopieringskonstruktor anropade förut en dold hjälpfunktion som libc++.so
inte exporterar).  Matchning: mallen, argumenten som prefix (namn jämförs manglade: vcc kan ha två
instanser av char_traits<char>), medlemsnamn och antal parametrar.  cxx-strict 386.

## Status 97 (2026-11-14)
En trivialt kopierbar klass med basklass skickas och returneras som värde när ingen bas har
flyttalsdata (basens bytes klassas då som heltal, som clang; anrop över kompilatorgränsen kontrollerade).
`B::operator=(x)` för en klass utan egen tilldelning: den implicita, basdelobjektets bytes
(json_base_class_t).  cxx-strict 377 (386).

## Status 98 (2026-11-14)
Medlemmar av en partiell specialisering definierade utanför klassen (`template<class A> void
V<bool, A>::push(...)`, libc++:s vector<bool>) registreras åtskilda från primärmallens; en instans av den
partiella specialiseringen binder definitionens parametrar till det matchningen deducerade (sparat på
instansen).  cxx-strict 373 (377).

## Status 99 (2026-11-14)
`__builtin_invoke(f, args...)` (libc++:s `__invoke`, `__invoke_result`, `is_invocable`): anropet f(args...)
för en funktion, funktionspekare eller ett objekt med operator(); fel antal argument eller ett aritmetiskt
värde är ogiltigt (substitutionsfel).  En funktion i kopieringsinitiering av en klass förfaller till en
pekare (`std::function<...> f = cb;`).  Utanför-klass-definitioner: en med parameterpack matchar alla
antal ≥ n−1, flera med samma antal skiljs på referenssorten per parameter (`function(const function&)`
mot `function(function&&)`), ett klasspack i parametrarna (`_ArgTypes... __arg`) blir ett pack av
parametrarna, och en partiell specialiserings medlemsmallar hittas och binds (deducerade pack expanderas).
cxx-strict: 3/70, 342 saknade (373).  (Ett försök att bygga `std::addressof(x)` som `&x` direkt bröt
libc++:s `__static_fancy_pointer_cast` i json-testerna och togs bort.)

## Status 100 (2026-11-14)
`declaration.c` (4 900 rader), `expression.c` (2 600) och `instantiate.c` (3 500) delade som codegen/ i
`frontend/cxx/declaration/` (14 filer), `expression/` (9) och `instantiate/` (12), högst 751 rader per fil,
gemensamma typer och prototyper i `declaration.h`, `expression.h`, `instantiate.h`.  Ren flytt: -S-utdata
byte för byte lika med bygget före delningen på unit-algorithms, unit-hash, unit-noexcept; varje fil
kompilerar ensam (`VCC_UNITY=`).  cxx-run 77/77, cxx-parse 38/38, cxx-mangle 36; cxx-strict oförändrat.

## Status 101 (2026-11-14)
Konstruktormallar definierade utanför en partiell specialisering (`function<_Rp(_ArgTypes...)>::function(_Fp)`)
hittas (registrerade under klassens namn), även när primärmallen bara är deklarerad, och tolkas som
konstruktorer (ingen returtyp).  Kroppar vcc aldrig tolkade: 53 → 24; summan oförändrad 342 (de tolkas nu och
stannar vid nästa sak: std::function når `typeid(_Fp)` i `__func::target_type`, typeid saknas).

## Status 102 (2026-11-14)
`typeid(T)` och `typeid(e)` av en typ som inte är en polymorf klass: ett const std::type_info-lvärde, typens
_ZTI-objekt; för funktionstyper och pekare till funktioner definieras _ZTS/_ZTI svaga
(__function_type_info, __pointer_type_info), som clang.  Namn och likhet som clangs (`i 1S PFbiiE`).
cxx-strict 342 (std::function stannar nu vid pack `_ArgTypes&&... __arg` i klassmedlemmar).
Hastighet: cxx-strict:s kompilering, bara vcc, en process per TU utan delade huvudfiler: 29,5 s
väggtid summerat (CPU ungefär lika) mot clang -O2:s 243 s, ~8x; med huvudfilerna en gång per
konfiguration (fork-servern) var det 10 s, ~24x.  30x kräver både färdig täckning och den delningen
(eller en snabbare vcc): ingen komplett mätning ännu.

## Status 103 (2026-11-14)
Pekare till medlem (`R C::*`) får en egen opak (ofullständig) typ i stället för okänd, och deduktion av ett
sådant mönster mot ett argument som inte är en medlemspekare misslyckas (förut "inte känt", vilket
blockerade libc++:s `__function::__is_null`).  `std::function<bool(int,int)> f = cb; g(f); g(1,3)` går
nu igenom med bara vcc:s kod.  cxx-strict 342 (oförändrat i summa).

## Status 104 (2026-11-14)
Structured bindings `auto [a, b] = e;` (även `auto &[...]`, `const auto &[...]`): en dold variabel
`__vcc_sb_N` med deklarationens typ och initierare, och varje namn en lokal referens till nästa egna,
namngivna medlem (std::pair:s first/second, ett aggregats medlemmar; en referensmedlem: det den refererar
till).  Tupel-lika klasser via get<i> och arrayer: inte än.  libc++:s `__tree` (map/set-insättning) går
förbi `auto [__parent, __child] = __find_equal(...)`.  cxx-strict 338 (342).

## Status 105 (2026-11-14)
En lambdas parametrar kan vara ett pack av ett omgivande pack (`[this](_Args&&... __args2)`): ett pack av
värden för kroppen, och för koden ett pack av parametrarnas symboler.  Rätt i leksaksfall (lambda i en
medlemsmall av en klassmall, skickad till en mall); libc++:s `__tree::__emplace_unique` via
`__try_key_extraction` stannar ändå vid `__args2` (orsaken inte funnen än), så std::map::emplace och
operator[] saknas fortfarande.  cxx-strict 338.

## Status 106 (2026-11-14)
Felkod rättad: en tom klass värdeinitieras eller tilldelas utan att skriva sin byte (med
[[no_unique_address]] kan den ligga på en annan medlems plats: `std::unique_ptr`:s `__deleter_` på
`__ptr_`:s adress; `unique_ptr<int> p(new int(7)); *p` kraschade), även i aggregatinitiering.  En
arraymedlem från `{}` nollställs (`__compressed_pair_padding`).  En mall vcc inte kan deducera står inte
i vägen för en kandidat som matchar alla argument exakt, när mallens icke-beroende parameter kräver en
konvertering (`impl(__priority_tag<0>, ...)`).  `__atomic_add_fetch`/`__atomic_load_n` och liknande:
typade, och byggda som C-frontendens noder (libc++:s referensräkning).  cxx-strict: 3/70, 330 (338).

## Status 107 (2026-11-14)
std::map::emplace/operator[] med bara vcc: två orsaker kvar.  (1) Beroende på ordning: när
`pair<const int, int>`-konstruktorinstansen `pair(_U1&&, _U2&&)` skapas först inne i map:s väg
(allocator_traits/construct_at) får den ingen typ (anropet byggs inte); skapas den först i vanlig kod går
det.  (2) I `__tree::__emplace_unique`:s andra lambda (`[this](_Args&&... __args2)`) blir `__args2`
otypat, trots att samma form fungerar i reducerade fall.  Båda nästa steg.

## Status 108 (2026-11-14)
Funktionsmallsinstansiering återställer `c->gen_instance` (förut nollställd): en instansiering mitt i en
annan instans signatur (`explicit(!__is_implicit<...>())` i pair:s konstruktor) gjorde att den yttre
instansen aldrig fick sin typ, beroende på vilken kod som först namngav den.  `construct_at<pair<const int,
int>, int, int>` byggs nu i std::map:s väg.  Kvar för map: lambdan med `__args2`; fold-uttryck
(`(a + ...)`) saknas.  cxx-strict 330.

## Status 109 (2026-11-14)
std::map fungerar med vcc ensamt (emplace, `operator[]`, find, iteration: mp.cpp ger 8).  Det som saknades:
- `x->m` på ett objekt av klasstyp: `operator->` anropas (igen så länge resultatet är en klass,
  [over.ref]); förut blev objektets typ okänd, så `__h->__get_value()` och varje `it->first` föll.
- `return g();` till en annan klass (`pair<__tree_iterator, bool>` till `pair<__map_iterator, bool>`):
  den konverterande konstruktorn, i registret och via adress.  Ett anropsresultat i register är ett
  prvalue (`cxx_gen_prv_add` utan anrop att sikta), så `pair(pair<U1, U2>&&)` väljs.  Vid kodgenerering
  (`skip_explicit = 2`) räknas en `explicit(expr)`-konstruktor som konverterande: koden är redan godtagen.
- `f()` före `f() const` avgörs före mallreglerna (implicita objektargumentet, [over.match.best]/2.1):
  regeln "definitionen står för deklarationen" valde den const inline-versionen framför den icke-const
  som definieras utanför klassen, och `__find_equal` const anropade sig själv i oändlighet.
- `cxx_gen_call` behåller uttryckets typ: att bygga anropet instansierar kroppar som skrev över
  `c->etype`, så `auto [__dummy, __match] = __find_equal(__key)` saknade typ.
- En medlemsmall i en vanlig klass definierad utanför den (`template<class K> int T::f(...) {}`) ger
  deklarationen sin kropp (`cxx_ftemplate_join_declarations`, bara när en deklaration passar).  Kvar:
  ett anrop före definitionen (m2/m3-fallen) har redan gjort sin instans av deklarationen.
- Mangling: en mall som substitutionskandidat nycklas på mallen, inte entiteten (klassens eget namn i
  den är en annan entitet): `Pr<MI,bool>::Pr(Pr<U,V>&&)` blir `...OS_IT_T0_E` som clangs.
Ny test `run/arrow-convert.cpp`.  cxx-strict: 347 funktioner utan kod (330 förut), men jämfört funktion
för funktion med förra binären: 262 nya kompileras, 11 saknas som förut aldrig nåddes (inga regressioner).
Ny lucka sedd: en arraymedlem från en klammerlista i en mem-initierare (`x{a, 0, 0}`).

## Status 110 (2026-11-14)
- En explicit specialisering deklarerad efter att klassen namngetts (`std::char_traits<char>` via
  string_views standardargument i `__fwd/string_view.h`) definierar den klass namnet gjorde
  (`cxx_instance_named`), i stället för att lämna den som en ofullständig instans av primärmallen:
  `_Traits::compare(...)` hittade ingenting, så varje strängjämförelse föll.
- `__is_trivially_equality_comparable` svarar 0 för alla typer.  Clangs ja skulle välja
  `__find_vectorized` och liknande på `ext_vector_type`-vektorer, som C++-fronten inte har än;
  nej tar elementvisa slingor, som alltid är rätt.  (Vektortyper i C++-fronten: egen uppgift.)
- C: en vektor av const-element är inte själv const (`ext_vector_type` och `vector_size`, som clang).
- `static_cast<T>(x)` till en klass: ett temporärt objekt direktinitierat från x; ett prvalue av T är
  objektet självt (`static_cast<strong_ordering>(a <=> 0)`).  Bara static_cast: `T(x)` och `(T)x`
  bygger sina på annat håll.
- `T(x)` genom T:s kopieringskonstruktor från en klass som konverterar till T: objektet är
  konverteringsfunktionens resultat ([dcl.init.general]/16.6.1), ingen kopia (string_view(str)).
Tester: `run/spec-after-use.cpp`, `vcc/vector_const_element.c`.  cxx-strict 338 (347): 140 funktioner
kompileras nu, 10 nya saknas (strömmar och binärformat som inte nåddes förut).

## Status 111 (2026-11-14)
- Anrop som hittas genom argumentberoende uppslagning byggdes aldrig (bara typades): nu
  `cxx_gen_call` på det ADL valde, för klassers namnrymder och dolda vänner.  ADL tittar även i en
  uppräkningstyps namnrymd ([basic.lookup.argdep]/3.3: `make_error_condition(errc)`).
- En uppräknares nod har uppräkningstypen (inte den underliggande): `ec == errc::x` konverterar via
  `error_condition`s konstruktormall för uppräkningar.
- `std::common_comparison_category<Ts...>::type` räknas ut från standardens definition ([cmp.common]):
  libc++:s kropp behöver konstantevaluering vcc saknar (en slinga över en array i en constexpr-funktion).
  Därmed `three_way_comparable`, `compare_three_way` och `<=>` på pekare.
- Koncept med standardargument (`three_way_comparable<T>` = `<T, partial_ordering>`).
- Två mallar som är lika specialiserade, den ena med requires-klausul: den begränsade väljs
  ([temp.func.order]/6.2.2).  Kvar: clang manglar requires-klausulen (`IiQ5same2IT_EE`), vcc inte.
Test: `run/adl-calls.cpp`.  cxx-strict 348 funktioner (338): 43 fler kompileras, inga nya fel; ökningen är
anrop från nyss byggd kod till funktioner vcc aldrig instansierar ("no body seen", 44, nu störst).

## Status 112 (2026-11-14)
- Medlemsfunktioner med härledd returtyp (`auto f() { ... }` i klassen): förut fick de aldrig en typ (en
  `is_auto` som uteslöt klassmedlemmar), så varje anrop föll.  `cxx_late_deduce` tolkar kroppen en gång
  utan kod (`ret_capture`) och sätter funktionens typ, innan koden byggs: vid klassens slut, eller för en
  instans när ett anrop eller namnet behöver typen.  Ännu inte: en användning i klassen före definitionen.
- `static operator()` (C++23, libc++:s jämförare): objektet evalueras men skickas inte med.
- `if (T x = e; cond) ... else ...`: x syns i else-grenen också ([stmt.if]/3).
- Värdemallparametrar vars typ är klassens medlemstypedef (`template <size_type N>`): typen tolkas i
  bindningens scope (`cxx_bind_value_type`), och instansens argument manglas med den (`ILm16E`).
- `alias<T>::member` med en alias som ger en icke-klass: ett substitutionsfel, inte okänt
  (`__remove_const_ref_t<unsigned char>::first_type`): `std::set<unsigned char>` fungerar.
- `__builtin_lt_synthesizes_from_spaceship(const L&, const R&)` som clangs: binäroperatorns val.
Test: `run/auto-members.cpp`.  cxx-strict 345 (348): 36 fler kompileras, 1 ny (`auto __res = __lhs <=> __rhs`).
Sedda luckor: en instans av en C-variadisk funktionsmall (`f(Arg, ...)`) definieras aldrig.

## Status 113 (2026-11-14)
- Byggfunktioner som C-tabellen känner men C++-tabellen inte: biblioteksfunktioner (`__builtin_ceill` anropar
  `ceill`; clangs Builtins.def-signaturer via `cc_library_builtin_info`), oändlighet och tyst NaN som
  konstanter (`__builtin_huge_valf`, `__builtin_inf`, `__builtin_nan("")`).  Kvar: `__builtin_nans`.
- En funktion som namnges utan att anropas direkt (`(std::numeric_limits<T>::min)()`, `&X::f`) är använd:
  en instans medlemskropp instansieras (`cxx_gen_function_value`).
cxx-strict 338 (345): 190 funktioner till kompileras (mest `<cmath>`/`<limits>`), inga nya fel.

## Status 114 (2026-11-14)
- `ptr != nullptr` har typen bool (std::nullptr_t är en struct här; == och != jämför som en nollpekare,
  [expr.eq]/2): `__builtin_expect(!!(ptr != nullptr), 1)` i json:s `get_ref_impl`.
- Deduktion `const T *` från `int *` (kvalifikationskonvertering, [temp.deduct.call]/4.2):
  `exception::diagnostics(const BasicJsonType *)`.
- `__is_trivially_assignable`/`__is_trivially_constructible` för en klass från sig själv: nej med en
  användarskriven kopierings- eller flyttoperation ([class.copy.ctor]/11, [class.copy.assign]/9) eller en
  defaultad som inte är trivial; förut sa POD-layout ja (`pair<const string, json>`, `std::string`, json),
  och `pair<int, int>`s tilldelning var okänd, så `__uninitialized_allocator_copy_impl` föll.  Nu som clang.
cxx-strict 322 (338): 82 fler kompileras, 6 nya som nyss nåddes.

## Status 115 (2026-11-14)
- `a <=> b` utan `operator<=>(a, b)`: den omvända kandidaten, `0 <=> (b <=> a)` ([over.match.oper]/3.4;
  `"text" <=> string`): ordningen vänds genom `__value_` (-1 och 1 byter plats).
- En pack efter namnlösa mallparametrar som ges explicit (`__try_key_extraction_impl<K, true, R>(...)`)
  deduceras; förut avvisades den.  `std::map<std::string, T, std::less<>>` med `"a"` som nyckel fungerar.
- Aggregat på namnrymdsnivå från klammerlista (`const P p = {1, 2};`, `const O O::a{-1};`): initierarlistan
  (`cxx_gen_record_initializer`, medlemmar som är skalärer); förut definierades variabeln aldrig.
Test: `run/reversed-packs.cpp`.  cxx-strict 322.

## Status 116 (2026-11-14)
- `c ? &x : nullptr` har pekarens typ ([expr.cond]/7.3), `c ? p : q` med `T *` och `const T *` den mer
  kvalificerade: json:s `get_impl_ptr` (statement.c:331, förut den vanligaste orsaken).
cxx-strict 291 (322): 59 fler kompileras, inga nya fel.

## Status 117 (2026-11-14)
- En vänfunktionsmall definierad i sin klass (`__wrap_iter`s `operator-`) gav instanser som medlemmar, så
  anropet byggdes med objekt plus ett argument och föll; nu en instans i klassens namnrymd (`is_friend`).
- `f(T, T)` är mer specialiserad än `f(T, U)` när parametrarna binds lika (`__distance(_RandIter, _RandIter)`
  före `(_InputIter, _Sent)`).
- Ett requires-uttryck vars krav fallerar är falskt, inget runt det felformat: `member_missing`/`einvalid`
  återställs efter det (förut gjorde `__primary_template<iterator_traits<T>>` signaturen med
  `iter_difference_t<T>` till ett substitutionsfel).
Test: `run/friend-templates.cpp`.  cxx-strict 274 (291): 69 fler kompileras, 2 nya som nyss nåddes.

## Status 118 (2026-11-14)
Rättelse: "std::map fungerar" i Status 109-115 byggde på `wf.sh`, som fyller i clangs funktioner som svaga
symboler; med vcc:s objekt ensamt saknades `__find_equal` och `map::operator[]`.  Leksakerna körs nu med
vcc:s objekt ensamt (länkat mot libc++).
- En medlemsmall i en klassmall med flera definitioner utanför klassen med samma namn (`__find_equal(k)` och
  `__find_equal(hint, dummy, k)`): den med deklarationens antal parametrar och const.  `std::set` och
  `map::emplace` länkar och kör nu med vcc ensamt.
- `cxx_ooc_refs_agree` hittar parameterlistan efter `operator[]` och andra operatornamn.
- Aggregat med referensmedlemmar (`R{x}` för `struct R { int &r; }`): referentens adress lagras.
- En klasstyp som resultat kompletteras innan den bedöms (`cxx_gen_value_ok`).
Kvar för `map::operator[]` och hela `std::tuple`: en partiell specialisering vars mönster använder en alias
med en pack (`__tuple_impl<__index_sequence<I...>, T...>`) matchas aldrig, så `tuple<int>` får ingen layout.
