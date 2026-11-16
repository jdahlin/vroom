# 15.2 Conditional inclusion [cpp.cond]

[Contents](README.md) · up: [15 Preprocessing directives \[cpp\]](cpp.md) · prev: [15.1 Preamble \[cpp.pre\]](cpp.pre.md) · next: [15.3 Source file inclusion \[cpp.include\]](cpp.include.md)

```bnf
defined-macro-expression:
    'defined' identifier
    'defined (' identifier ')'
```

```bnf
h-preprocessing-token:
    any preprocessing-token other than '>'
```

```bnf
h-pp-tokens:
    h-preprocessing-token
    h-pp-tokens h-preprocessing-token
```

```bnf
header-name-tokens:
    string-literal
    '<' h-pp-tokens '>'
```

```bnf
has-include-expression:
    '__has_include' '(' header-name ')'
    '__has_include' '(' header-name-tokens ')'
```

```bnf
has-attribute-expression:
    '__has_cpp_attribute (' pp-tokens ')'
```

**1** The expression that controls conditional inclusion shall be an integral constant expression except that identifiers (including those lexically identical to keywords) are interpreted as described below[^1] and it may contain zero or more *defined-macro-expression*s and/or *has-include-expression*s and/or *has-attribute-expression*s as unary operator expressions.

**2** A *defined-macro-expression* evaluates to `1` if the identifier is currently defined as a macro name (that is, if it is predefined or if it has one or more active macro definitions ([[cpp.import]](cpp.import.md)), for example because it has been the subject of a `#define` preprocessing directive without an intervening `#undef` directive with the same subject identifier), `0` if it is not.

**3** The second form of *has-include-expression* is considered only if the first form does not match, in which case the preprocessing tokens are processed just as in normal text.

**4** The header or source file identified by the parenthesized preprocessing token sequence in each contained *has-include-expression* is searched for as if that preprocessing token sequence were the *pp-tokens* in a `#include` directive, except that no further macro expansion is performed. If such a directive would not satisfy the syntactic requirements of a `#include` directive, the program is ill-formed. The *has-include-expression* evaluates to `1` if the search for the source file succeeds, and to `0` if the search fails.

**5** Each *has-attribute-expression* is replaced by a non-zero *pp-number* matching the form of an *integer-literal* if the implementation supports an attribute with the name specified by interpreting the *pp-tokens*, after macro expansion, as an *attribute-token*, and by `0` otherwise. The program is ill-formed if the *pp-tokens* do not match the form of an *attribute-token*.

**6** For an attribute specified in this document, the value of the *has-attribute-expression* is given by [Table 18](cpp.cond.md#tab:cpp.cond.ha). For other attributes recognized by the implementation, the value is implementation-defined. [*Note 1*: It is expected that the availability of an attribute can be detected by any non-zero result. — *end note*]

<a id="tab:cpp.cond.ha"></a>
**Table 18: `__has_cpp_attribute` values** [tab:cpp.cond.ha]

| Attribute | Value |
| --- | --- |
| `carries_dependency` | `200809L` |
| `deprecated` | `201309L` |
| `fallthrough` | `201603L` |
| `likely` | `201803L` |
| `maybe_unused` | `201603L` |
| `no_unique_address` | `201803L` |
| `nodiscard` | `201907L` |
| `noreturn` | `200809L` |
| `unlikely` | `201803L` |

**7** The `#ifdef` and `#ifndef` directives, and the `defined` conditional inclusion operator, shall treat `__has_include` and `__has_cpp_attribute` as if they were the names of defined macros. The identifiers `__has_include` and `__has_cpp_attribute` shall not appear in any context not mentioned in this subclause.

**8** Each preprocessing token that remains (in the list of preprocessing tokens that will become the controlling expression) after all macro replacements have occurred shall be in the lexical form of a token ([[lex.token]](lex.token.md)).

**9** Preprocessing directives of the forms

```bnf
'# if' constant-expression new-line groupₒₚₜ
'# elif' constant-expression new-line groupₒₚₜ
```

check whether the controlling constant expression evaluates to nonzero.

**10** Prior to evaluation, macro invocations in the list of preprocessing tokens that will become the controlling constant expression are replaced (except for those macro names modified by the `defined` unary operator), just as in normal text. If the token `defined` is generated as a result of this replacement process or use of the `defined` unary operator does not match one of the two specified forms prior to macro replacement, the behavior is undefined.

**11** After all replacements due to macro expansion and evaluations of *defined-macro-expression*s, *has-include-expression*s, and *has-attribute-expression*s have been performed, all remaining identifiers and keywords, except for `true` and `false`, are replaced with the *pp-number* `0`, and then each preprocessing token is converted into a token. [*Note 2*: An alternative token ([[lex.digraph]](lex.digraph.md)) is not an identifier, even when its spelling consists entirely of letters and underscores. Therefore it is not subject to this replacement. — *end note*]

**12** The resulting tokens comprise the controlling constant expression which is evaluated according to the rules of [[expr.const]](expr.const.md) using arithmetic that has at least the ranges specified in [[support.limits]](lib/support.limits.md). For the purposes of this token conversion and evaluation all signed and unsigned integer types act as if they have the same representation as, respectively, `intmax_t` or `uintmax_t` ([[cstdint]](lib/cstdint.md)). [*Note 3*: Thus on an implementation where `std::numeric_limits<int>::max()` is `0x7FFF` and `std::numeric_limits<unsigned int>::max()` is `0xFFFF`, the integer literal `0x8000` is signed and positive within a `#if` expression even though it is unsigned in translation phase 7 ([[lex.phases]](lex.phases.md)). — *end note*] This includes interpreting *character-literal*s, which may involve converting escape sequences into execution character set members. Whether the numeric value for these *character-literal*s matches the value obtained when an identical *character-literal* occurs in an expression (other than within a `#if` or `#elif` directive) is implementation-defined. [*Note 4*: Thus, the constant expression in the following `#if` directive and `if` statement ([[stmt.if]](stmt.if.md)) is not guaranteed to evaluate to the same value in these two contexts:

```cpp
#if 'z' - 'a' == 25
if ('z' - 'a' == 25)
```

— *end note*] Also, whether a single-character *character-literal* may have a negative value is implementation-defined. Each subexpression with type `bool` is subjected to integral promotion before processing continues.

**13** Preprocessing directives of the forms

```bnf
'# ifdef' identifier new-line groupₒₚₜ
'# ifndef' identifier new-line groupₒₚₜ
```

check whether the identifier is or is not currently defined as a macro name. Their conditions are equivalent to `#if` `defined` *identifier* and `#if` `!defined` *identifier* respectively.

**14** Each directive’s condition is checked in order. If it evaluates to false (zero), the group that it controls is skipped: directives are processed only through the name that determines the directive in order to keep track of the level of nested conditionals; the rest of the directives’ preprocessing tokens are ignored, as are the other preprocessing tokens in the group. Only the first group whose control condition evaluates to true (nonzero) is processed; any following groups are skipped and their controlling directives are processed as if they were in a group that is skipped. If none of the conditions evaluates to true, and there is a `#else` directive, the group controlled by the `#else` is processed; lacking a `#else` directive, all the groups until the `#endif` are skipped.[^2]

**15** [*Example 1*: This demonstrates a way to include a library `optional` facility only if it is available:

```cpp
#if __has_include(<optional>)
#  include <optional>
#  if __cpp_lib_optional >= 201603
#    define have_optional 1
#  endif
#elif __has_include(<experimental/optional>)
#  include <experimental/optional>
#  if __cpp_lib_experimental_optional >= 201411
#    define have_optional 1
#    define experimental_optional 1
#  endif
#endif
#ifndef have_optional
#  define have_optional 0
#endif
```

— *end example*]

**16** [*Example 2*: This demonstrates a way to use the attribute `[[acme::deprecated]]` only if it is available.

```cpp
#if __has_cpp_attribute(acme::deprecated)
#  define ATTR_DEPRECATED(msg) [[acme::deprecated(msg)]]
#else
#  define ATTR_DEPRECATED(msg) [[deprecated(msg)]]
#endif
ATTR_DEPRECATED("This function is deprecated") void anvil();
```

— *end example*]

[^1]: Because the controlling constant expression is evaluated during translation phase 4, all identifiers either are or are not macro names — there simply are no keywords, enumeration constants, etc.
[^2]: As indicated by the syntax, a preprocessing token shall not follow a `#else` or `#endif` directive before the terminating new-line character. However, comments may appear anywhere in a source file, including within a preprocessing directive.
