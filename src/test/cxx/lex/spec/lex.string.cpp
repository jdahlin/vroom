// RUN: %clang_cc1 -std=c++20 -fsyntax-only -verify %s
// RUN: %clang_cc1 -std=c++11 -fsyntax-only -verify %s
//
// The examples of [lex.string] (docs/cxx-spec/lex/lex.string.md), and phase
// 6's concatenations.  bench/cxx-lex.py checks each literal's type and every
// code unit against clang.

// [lex.string]/2 Note 1: R"delimiter((a|b))delimiter" is equivalent to "(a|b)"
const char *note1 = R"delimiter((a|b))delimiter";

// [lex.string]/3 Note 2: a source-file new-line in a raw string literal is a
// new-line; the splice is reverted
const char *note2 = R"(a\
b
c)";

// [lex.string]/4 Example 1
const char *example1a = R"a(
)\
a"
)a";
const char *example1b = R"(x = "\"y\"")";

// [lex.string]/11 Table 11: the encoding-prefix of a concatenation
const char16_t *table11_1 = u"a" u"b";
const char16_t *table11_2 = u"a" "b";
const char16_t *table11_3 = "a" u"b";
const char32_t *table11_4 = U"a" U"b";
const char32_t *table11_5 = U"a" "b";
const char32_t *table11_6 = "a" U"b";
const wchar_t *table11_7 = L"a" L"b";
const wchar_t *table11_8 = L"a" "b";
const wchar_t *table11_9 = "a" L"b";

// [lex.string]/11 Example 2: "Characters in concatenated strings are kept
// distinct": two characters, '\xA' and 'B'
const char *example2 = "\xA" "B";

// an ordinary piece takes the other's encoding: U+00E9 (an extended
// character, then a universal-character-name) is one UTF-16 code unit,
// U+1F600 two
const char16_t *encoded = "é" u"\U0001F600";
const char16_t *encoded_ucn = "\u00e9" u"\U0001F600";

// [lex.string]/11: "If a UTF-8 string literal token is adjacent to a wide
// string literal token, the program is ill-formed."
const char *utf8_wide = u8"a" L"b"; // expected-error {{unsupported non-standard concatenation of string literals}}

// [lex.ext]/8: the user-defined-string-literals of a concatenation have the
// same ud-suffix
struct S {};
S operator""_s(const char *, decltype(sizeof 0));
S same = "a"_s "b" "c"_s;
S differing = "a"_s "b"_t; // expected-error {{differing user-defined suffixes ('_s' and '_t') in string literal concatenation}}
