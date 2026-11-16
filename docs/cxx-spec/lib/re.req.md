# 30.3 Requirements [re.req]

[Contents](../README.md) · up: [30 Regular expressions library \[re\]](re.md) · prev: [30.2 Definitions \[re.def\]](re.def.md) · next: [30.4 Header `<regex>` synopsis \[re.syn\]](re.syn.md)

**1** This subclause defines requirements on classes representing regular expression traits. [*Note 1*: The class template `regex_traits`, defined in [[re.traits]](re.traits.md), meets these requirements. — *end note*]

**2** The class template `basic_regex`, defined in [[re.regex]](re.regex.md), needs a set of related types and functions to complete the definition of its semantics. These types and functions are provided as a set of member *typedef-name*s and functions in the template parameter `traits` used by the `basic_regex` class template. This subclause defines the semantics of these members.

**3** To specialize class template `basic_regex` for a character container `CharT` and its related regular expression traits class `Traits`, use `basic_regex<CharT, Traits>`.

**4** In [Table 135](re.req.md#tab:re.req) `X` denotes a traits class defining types and functions for the character container type `charT`; `u` is an object of type `X`; `v` is an object of type `const X`; `p` is a value of type `const charT*`; `I1` and `I2` are input iterators ([[input.iterators]](input.iterators.md)); `F1` and `F2` are forward iterators ([[forward.iterators]](forward.iterators.md)); `c` is a value of type `const charT`; `s` is an object of type `X::string_type`; `cs` is an object of type `const X::string_type`; `b` is a value of type `bool`; `I` is a value of type `int`; `cl` is an object of type `X::char_class_type`, and `loc` is an object of type `X::locale_type`.

<a id="tab:re.req"></a>
**Table 135: Regular expression traits class requirements** [tab:re.req]

| Expression | Return type | Assertion/note pre-/post-condition |
| --- | --- | --- |
| `X::char_type` | `charT` | The character container type used in the implementation of class template `basic_regex`. |
| `X::string_type` | `basic_string<charT>` |  |
| `X::locale_type` | A copy constructible type | A type that represents the locale used by the traits class. |
| `X::char_class_type` | A bitmask type ([[bitmask.types]](bitmask.types.md)). | A bitmask type representing a particular character classification. |
| `X::length(p)` | `size_t` | Yields the smallest `i` such that `p[i] == 0`. Complexity is linear in `i`. |
| `v.translate(c)` | `X::char_type` | Returns a character such that for any character `d` that is to be considered equivalent to `c` then `v.translate(c) == v.translate(d)`. |
| `v.translate_nocase(c)` | `X::char_type` | For all characters `C` that are to be considered equivalent to `c` when comparisons are to be performed without regard to case, then `v.translate_nocase(c) == v.translate_nocase(C)`. |
| `v.transform(F1, F2)` | `X::string_type` | Returns a sort key for the character sequence designated by the iterator range `[F1, F2)` such that if the character sequence `[G1, G2)` sorts before the character sequence `[H1, H2)` then `v.transform(G1, G2) < v.transform(H1, H2)`. |
| `v.transform_primary(F1, F2)` | `X::string_type` | Returns a sort key for the character sequence designated by the iterator range `[F1, F2)` such that if the character sequence `[G1, G2)` sorts before the character sequence `[H1, H2)` when character case is not considered then `v.transform_primary(G1, G2) < v.transform_primary(H1, H2)`. |
| `v.lookup_collatename(F1, F2)` | `X::string_type` | Returns a sequence of characters that represents the collating element consisting of the character sequence designated by the iterator range `[F1, F2)`. Returns an empty string if the character sequence is not a valid collating element. |
| `v.lookup_classname(F1, F2, b)` | `X::char_class_type` | Converts the character sequence designated by the iterator range `[F1, F2)` into a value of a bitmask type that can subsequently be passed to `isctype`. Values returned from `lookup_classname` can be bitwise OR’ed together; the resulting value represents membership in either of the corresponding character classes. If `b` is `true`, the returned bitmask is suitable for matching characters without regard to their case. Returns `0` if the character sequence is not the name of a character class recognized by `X`. The value returned shall be independent of the case of the characters in the sequence. |
| `v.isctype(c, cl)` | `bool` | Returns `true` if character `c` is a member of one of the character classes designated by `cl`, `false` otherwise. |
| `v.value(c, I)` | `int` | Returns the value represented by the digit *c* in base *I* if the character *c* is a valid digit in base *I*; otherwise returns `-1`. [*Note 2*: The value of *I* will only be 8, 10, or 16. — *end note*] |
| `u.imbue(loc)` | `X::locale_type` | Imbues `u` with the locale `loc` and returns the previous locale used by `u` if any. |
| `v.getloc()` | `X::locale_type` | Returns the current locale used by `v`, if any. |

**5** [*Note 3*: Class template `regex_traits` meets the requirements for a regular expression traits class when it is specialized for `char` or `wchar_t`. This class template is described in the header `<regex>`, and is described in [[re.traits]](re.traits.md). — *end note*]
