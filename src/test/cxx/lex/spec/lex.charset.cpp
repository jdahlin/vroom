// RUN: %clang_cc1 -std=c++23 -fsyntax-only -verify %s
// RUN: %clang_cc1 -std=c++20 -pedantic -fsyntax-only -verify=expected,pre23 %s
// RUN: %clang_cc1 -std=c++11 -fsyntax-only -verify %s
//
// [lex.charset] (docs/cxx-spec/lex/lex.charset.md) universal-character-names,
// and C++23's named ones (P2071R2), clang's extension before C++23.

// [lex.charset]/2: "\\u hex-quad" and "\\U hex-quad hex-quad"
char32_t a = U'\u00e9', b = U'\U0001F600';

// "The program is ill-formed if that number is not a code point or if it
// is a surrogate code point."
char32_t surrogate = U'\ud800'; // expected-error {{invalid universal character}}
char32_t beyond = U'\U00110000'; // expected-error {{invalid universal character}}

// named-universal-character: names, the names a rule makes, aliases
char32_t n1 = U'\N{LATIN SMALL LETTER E WITH ACUTE}'; // pre23-warning {{named escape sequences are a C++23 extension}}
char32_t n2 = U'\N{CJK UNIFIED IDEOGRAPH-4E00}'; // pre23-warning {{named escape sequences are a C++23 extension}}
char32_t n3 = U'\N{HANGUL SYLLABLE GAG}'; // pre23-warning {{named escape sequences are a C++23 extension}}
char32_t n4 = U'\N{LINE FEED}'; // pre23-warning {{named escape sequences are a C++23 extension}}
const char16_t *n5 = u"\N{GRINNING FACE}!"; // pre23-warning {{named escape sequences are a C++23 extension}}

// names are matched exactly
char32_t n6 = U'\N{latin small letter a}'; // expected-error {{'latin small letter a' is not a valid Unicode character name}} pre23-warning 0-1 {{named escape}}
char32_t n7 = U'\N{}'; // expected-error {{delimited escape sequence cannot be empty}}

// in an identifier
int caf\N{LATIN SMALL LETTER E WITH ACUTE} = 1; // pre23-warning {{named escape sequences are a C++23 extension}}
