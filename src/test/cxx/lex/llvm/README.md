# clang's lexer tests

Copied unchanged from llvm-project at bb04dac04a0f9e9b99ffc2e28b4806d4f98e7966
(the commit `bench/fetch-testsuites.sh llvm` pins), under the Apache License
v2.0 with LLVM Exceptions (`LICENSE.TXT`):

- `Lexer/`: `clang/test/Lexer/*.cpp`, except the `has_feature_*` and
  `cross-windows-*` tests (they test the preprocessor's `__has_feature` and
  the driver, not lexing), `has_extension_cxx.cpp` (the same) and
  `update_consecutive_macro_crash.cpp` (a macro expansion crash).
- `CXX/lex/`: `clang/test/CXX/lex/`, the tests clang files under the
  standard's own subclause names (`lex.literal/lex.ext/p3.cpp` is
  `[lex.ext]/3`).

`bench/cxx-lex.py` runs each test's `%clang_cc1` RUN lines through
`build/.../cxxlex` and through clang, and compares preprocessing tokens,
tokens and lexical diagnostics.  clang, not the `expected-*` directives, is
the oracle: most of these tests also check the parser and Sema, which the C++
front end does not have yet.
