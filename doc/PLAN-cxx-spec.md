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

Now: raw 68/97, tokens 63/97, diags 70/97.

## Steps

- **L0 fundamentals** (this PR): source files and presumed lines
  (`[lex.separate]`, `[cpp.line]`), diagnostics (`[intro.compliance]`),
  implementation-defined behavior, fundamental types (`[basic.fundamental]`).
- **L1 lexer** (this PR, partial): phases 1-3 complete in shape; phase 7 for
  identifiers, keywords, alternative tokens, operators, integer literals.
  Missing: values of character, string and floating literals (phases 5-6),
  `\N{}`, Unicode whitespace and homoglyph diagnostics, raw-string error
  recovery as clang, the remaining mismatches `bench/cxx-lex.py` lists.
- **L2** citation checker (`bench/cxx-spec-refs.py`: every `[label]/N`
  exists, every quoted production matches the spec's), in CI.
- **L3** the preprocessor (`[cpp]`) on these tokens, or an adapter from vcc's
  existing pp: to decide.
- Then `[basic]`, `[expr]`, `[stmt]`, `[dcl]`, ... in the order of the
  standard's clauses.

## Decisions (review of PR #307, 2026-11-16)

1. Files keep the dotted stable name (`lex/lex.icon.cpp`).
2. Comments quote the sentence each check enforces, not only cite it.
3. n4861 (C++20) stays the base text; later papers are cited where clang
   follows them.
4. Compliance first: optimize (SIMD, the existing lexer's techniques) only
   once the tests pass.
