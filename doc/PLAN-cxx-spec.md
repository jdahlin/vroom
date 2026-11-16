# C++ front end written against the standard

Status: proof of concept, for review of architecture, naming and comments
before more is built on it.  Replaces `src/frontend/cxx/` (PLAN-cxx.md) step
by step once it covers what that front end does.

## Conventions (to review)

- **Layout mirrors the standard.**  `src/frontend/cplusplus/<clause>/<stable
  name>.cpp` is one subclause: `lex/lex.icon.cpp` is `[lex.icon]`,
  `intro/intro.compliance.cpp` is `[intro.compliance]`.  A subclause too small
  for a file goes into its parent's.  Shared declarations: the public
  `src/include/frontend/cplusplus/cplusplus.h`, the internal
  `cplusplus-internal.h`.
- **Names are the standard's terms.**  `preprocessing_token`,
  `encoding_prefix`, `pp_number`, `ud_suffix_offset`, `integer_suffix`,
  `implementation_defined_behavior`, `standard_edition`,
  `translation_character`, `physical_characters`, `scan_raw_string`,
  `convert_integer_literal`.  Enumerators are the grammar's nonterminals with
  `-` as `_` (`preprocessing_token_kind::user_defined_string_literal`).
  clang's names appear only as data (diagnostic ids, `clang_token_name`).
- **Comments cite and quote** (Ladybird style): `[label]/N` is paragraph N of
  `docs/cxx-spec/.../label.md`, `[label]/N.M` item M of its list.  A function
  starts with the grammar it implements, copied from the spec (`_opt` for the
  subscript), and quotes the sentence a check enforces next to the check.
  Where clang departs from n4861 (a later paper applied as a DR, an
  extension), the comment says so and names the paper (P1949R7 for
  identifiers, P2290R3 for `\u{}`, P0330R8 for `z`).
- **Memory is held by owners** (`src/include/frontend/cplusplus/owned.h`:
  `owned_array`, `owned_buffer`, `owned_string`, RAII): no `free()`,
  `malloc()`, `realloc()` or `strdup()` outside that header.  The pre-commit
  hook `no-raw-allocation` enforces it, and CI runs it on every file.
- **C++20 without exceptions, RTTI or the standard library**: namespace
  `cplusplus`, `enum class`, classes with member functions, RAII.  The module
  is not built with `VCC_LANG=c`.
- **Data from clang once** (`src/tools/gen-cplusplus-data.py`): the text,
  class and group of every Lex/Common diagnostic, the -W group tree, the
  keyword table with its KEY* conditions (TokenKinds.def), XID_Start and
  XID_Continue (Unicode 16).
- **Phases folded, not materialized**: phases 1 and 2 are read through
  `lexer::peek`/`read` (a fast path for ordinary characters), as
  `[lex.phases]/1`'s footnote allows.

## Tests

`src/test/cxx/lex/llvm/`: clang's `test/Lexer/*.cpp` and `test/CXX/lex/`
(Apache 2.0 with LLVM exceptions; README there).  `bench/cxx-lex.py` (~2 s)
runs each `%clang_cc1` RUN line through `cxxlex` and clang and compares
preprocessing tokens (`-dump-raw-tokens`), tokens after `clang -E`
(`-dump-tokens`), and lexical diagnostics.  clang is the oracle, not the
`expected-*` directives (most of those tests need a parser).

It also checks values: `cxxlex -literal-claims` writes each literal's type
and value (integer, character, every code unit of a string) as
`_Static_assert`s that clang compiles (about 2000 in C++20 mode alone).
`src/test/cxx/lex/known-differences.txt` lists the runs whose difference from
clang is understood: the standard is followed where clang is not, or the
difference is a later phase's.

Now (2026-11-16): 121 runs of 62 tests, raw, tokens, values and diags all
121/121, 9 of them known differences.

## Steps

- **L0 fundamentals** (this PR): source files and presumed lines
  (`[lex.separate]`, `[cpp.line]`), diagnostics (`[intro.compliance]`),
  implementation-defined behavior, fundamental types (`[basic.fundamental]`).
- **L1 lexer** (done, this PR): phases 1-3; phase 5 for character and
  string literals (escapes, universal-character-names including C++23's
  `\N{}` with Unicode 16's names and aliases, UTF-8/16/32); phase 6
  (`lexer::convert_string_literals`: [lex.string]/11 Table 11, [lex.ext]/8);
  phase 7 for identifiers (XID per P1949R7, UAX #31's mathematical notation
  as clang's extension, look-alike and invisible character warnings),
  keywords, alternative tokens, operators, integer literals (values, Table 8
  types, `_BitInt` widths), floating-point literals (types, values rounded
  once to nearest, subnormals included).  Unicode white space beyond
  [lex.charset]/1 is white space as an extension.  Per-edition rules are
  named features (`diff/language-features.inc`).  Checked beyond the tests:
  3000 random floating-point literals and all 43490 Unicode names and
  aliases agree with clang.
- Our own tests: `src/test/cxx/lex/spec/`, one file per subclause, built from
  the standard's examples (`lex.string.cpp`).
- **L2** citation checker (`bench/cxx-spec-refs.py`: every `[label]/N`
  exists, every quoted production matches the spec's), in CI.
- **L3** phase 4, the preprocessor, written against `[cpp]` like the lexer
  (`src/frontend/cplusplus/cpp/`, one file per subclause), checked against
  clang the same way (decided 2026-11-16, replacing the earlier plan of an
  adapter from vcc's C preprocessor).  The context the lexer needs
  (header-names after `#include`/`import`, the end of a directive's line)
  comes from it.
  Status (2026-11-16, in progress): every subclause has its file: groups and
  directives (`cpp.pre`), conditional inclusion with the `#if` evaluator as
  intmax_t/uintmax_t (`cpp.cond`), inclusion with clang's search order
  (`cpp.include`), definitions and invocations (`cpp.replace`, `#undef` in
  it), substitution with `__VA_OPT__` (`cpp.subst`), `#`, `##` with
  placemarkers, rescanning as a stack of token contexts (`cpp.rescan`),
  `#line` and GNU line markers, `#error`/`#warning`, the phase-4 pragmas,
  `_Pragma`, the module and import directives, and clang's predefined macros
  per edition/GNU/MS/char8_t mode (`cpp/cpp.predefined.inc`, from clang -dM
  once).  Locations: `source_files` puts every file and every piece of
  scratch text in one space, so a token knows its file and the invocation it
  came from (clang's expansion location).  `cxxlex -dump-tokens` now runs
  phases 1-7 as `clang -cc1 -dump-tokens`; `cxxlex -E [-dM]`.
  `bench/cxx-pp.py`: clang's test/Preprocessor (run as C++) and
  test/CXX/cpp: 534 runs of 377 tests, tokens 462, diagnostics 354, macros
  495; 212 tests pass every run.  Left: `__has_feature`/`__has_builtin`/
  `__has_attribute` data from clang (stubs answer 0), `#pragma clang
  diagnostic`, `#embed`, UCNs in macro names as their characters, the
  leading-space and line-start flags in the remaining cases, the standard's
  examples as tests (`src/test/cxx/cpp/spec/`), known-differences.
- Then `[basic]`, `[expr]`, `[stmt]`, `[dcl]`, ... in the order of the
  standard's clauses.

## Decisions (review of PR #307, 2026-11-16)

1. Files keep the dotted stable name (`lex/lex.icon.cpp`).
2. Comments quote the sentence each check enforces, not only cite it.
3. n4861 (C++20) stays the base text; later papers are cited where clang
   follows them.
4. Compliance first: optimize (SIMD, the existing lexer's techniques) only
   once the tests pass.
