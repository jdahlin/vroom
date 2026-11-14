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
