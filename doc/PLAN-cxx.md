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

- `frontend/lex/` with a C++ mode: `::`, `.*`, `->*`, raw strings,
  digit separators, C++ keywords, alternative tokens.  Tables per mode, not
  a second lexer (`PLAN-tree.md`).
- pp: `__cplusplus`, `__has_cpp_attribute`, C++ predefines taken from clang
  once (`gen-ppdata.sh` for `-x c++`), libc++'s include paths.
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

- X0-X7: not started (2026-11-13).
