// RUN: %clang_cc1 -std=c++20 -fsyntax-only -verify %s
// RUN: %clang_cc1 -std=c++11 -fsyntax-only -verify %s
//
// [lex.name] (docs/cxx-spec/lex/lex.name.md): identifiers with extended
// characters, written in UTF-8 and as universal-character-names.  XID_Start
// and XID_Continue as P1949R7 (applied as a defect report) has them.

// [lex.name]/1: "An identifier is an arbitrarily long sequence of letters
// and digits."  Letters of other scripts, written either way
int été = 1, \u00e9t\u00e9_ucn = 2, αβγ = 3, 日本 = 4;
int x́ = 5; // a combining mark continues an identifier

// a combining mark cannot start one
int ́y = 6; // expected-error {{character <U+0301> not allowed at the start of an identifier}} expected-error {{expected unqualified-id}}

// [lex.charset]/2: a universal-character-name outside a literal may not
// name a basic source character
int \u0041bc = 7; // expected-error {{character 'A' cannot be specified by a universal character name}}

// clang's extensions: white space beyond [lex.charset]/1's, '$', and
// UAX #31's mathematical notation profile
int nbsp = 8; // expected-warning {{treating Unicode character as whitespace}}
int dollar$ = 9;
int ∂x₂ = 10; // expected-warning {{mathematical notation character <U+2202> in an identifier is a Clang extension}} expected-warning {{mathematical notation character <U+2082> in an identifier is a Clang extension}}

// a character that is no identifier character ends the identifier and is
// a preprocessing token of its own ([lex.pptoken]/1), here U+00B6
int z¶ = 11; // expected-error {{character <U+00B6> not allowed in an identifier}}
