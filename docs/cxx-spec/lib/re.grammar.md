# 30.13 Modified ECMAScript regular expression grammar [re.grammar]

[Contents](../README.md) · up: [30 Regular expressions library \[re\]](re.md) · prev: [30.12.2.4 Increment \[re.tokiter.incr\]](re.tokiter.incr.md) · next: [31 Atomic operations library \[atomics\]](atomics.md)

**1** The regular expression grammar recognized by `basic_regex` objects constructed with the ECMAScript flag is that specified by ECMA-262, except as specified below.

**2** Objects of type specialization of `basic_regex` store within themselves a default-constructed instance of their `traits` template parameter, henceforth referred to as `traits_inst`. This `traits_inst` object is used to support localization of the regular expression; `basic_regex` member functions shall not call any locale dependent C or C++ API, including the formatted string input functions. Instead they shall call the appropriate traits member function to achieve the required effect.

**3** The following productions within the ECMAScript grammar are modified as follows:

```bnf
ClassAtom:
  '-'
  ClassAtomNoDash
  ClassAtomExClass
  ClassAtomCollatingElement
  ClassAtomEquivalence

IdentityEscape:
  SourceCharacter but not 'c'
```

**4** The following new productions are then added:

```bnf
ClassAtomExClass:
  '[:' ClassName ':]'

ClassAtomCollatingElement:
  '[.' ClassName '.]'

ClassAtomEquivalence:
  '[=' ClassName '=]'

ClassName:
  ClassNameCharacter
  ClassNameCharacter ClassName

ClassNameCharacter:
  SourceCharacter but not one of '.' or '=' or ':'
```

**5** The productions *ClassAtomExClass*, *ClassAtomCollatingElement* and *ClassAtomEquivalence* provide functionality equivalent to that of the same features in regular expressions in POSIX.

**6** The regular expression grammar may be modified by any `regex_constants::syntax_option_type` flags specified when constructing an object of type specialization of `basic_regex` according to the rules in [Table 136](re.synopt.md#tab:re.synopt).

**7** A *ClassName* production, when used in *ClassAtomExClass*, is not valid if `traits_inst.lookup_classname` returns zero for that name. The names recognized as valid *ClassName*s are determined by the type of the traits class, but at least the following names shall be recognized: `alnum`, `alpha`, `blank`, `cntrl`, `digit`, `graph`, `lower`, `print`, `punct`, `space`, `upper`, `xdigit`, `d`, `s`, `w`. In addition the following expressions shall be equivalent:

```cpp
\d and [[:digit:]]

\D and [^[:digit:]]

\s and [[:space:]]

\S and [^[:space:]]

\w and [_[:alnum:]]

\W and [^_[:alnum:]]
```

**8** A *ClassName* production when used in a *ClassAtomCollatingElement* production is not valid if the value returned by `traits_inst.lookup_collatename` for that name is an empty string.

**9** The results from multiple calls to `traits_inst.lookup_classname` can be bitwise OR’ed together and subsequently passed to `traits_inst.isctype`.

**10** A *ClassName* production when used in a *ClassAtomEquivalence* production is not valid if the value returned by `traits_inst.lookup_collatename` for that name is an empty string or if the value returned by `traits_inst.transform_primary` for the result of the call to `traits_inst.lookup_collatename` is an empty string.

**11** When the sequence of characters being transformed to a finite state machine contains an invalid class name the translator shall throw an exception object of type `regex_error`.

**12** If the *CV* of a *UnicodeEscapeSequence* is greater than the largest value that can be held in an object of type `charT` the translator shall throw an exception object of type `regex_error`. [*Note 1*: This means that values of the form `"uxxxx"` that do not fit in a character are invalid. — *end note*]

**13** Where the regular expression grammar requires the conversion of a sequence of characters to an integral value, this is accomplished by calling `traits_inst.value`.

**14** The behavior of the internal finite state machine representation when used to match a sequence of characters is as described in ECMA-262. The behavior is modified according to any `match_flag_type` flags ([[re.matchflag]](re.matchflag.md)) specified when using the regular expression object in one of the regular expression algorithms ([[re.alg]](re.alg.md)). The behavior is also localized by interaction with the traits class template parameter as follows:

- During matching of a regular expression finite state machine against a sequence of characters, two characters `c` and `d` are compared using the following rules:

  - if `(flags() & regex_constants::icase)` the two characters are equal if `traits_inst.translate_nocase(c) == traits_inst.translate_nocase(d)`;
  - otherwise, if `flags() & regex_constants::collate` the two characters are equal if `traits_inst.translate(c) == traits_inst.translate(d)`;
  - otherwise, the two characters are equal if `c == d`.

- During matching of a regular expression finite state machine against a sequence of characters, comparison of a collating element range `c1-c2` against a character `c` is conducted as follows: if `flags() & regex_constants::collate` is `false` then the character `c` is matched if `c1 <= c && c <= c2`, otherwise `c` is matched in accordance with the following algorithm:

  ```cpp
  string_type str1 = string_type(1,
    flags() & icase ?
      traits_inst.translate_nocase(c1) : traits_inst.translate(c1));
  string_type str2 = string_type(1,
    flags() & icase ?
      traits_inst.translate_nocase(c2) : traits_inst.translate(c2));
  string_type str = string_type(1,
    flags() & icase ?
      traits_inst.translate_nocase(c) : traits_inst.translate(c));
  return traits_inst.transform(str1.begin(), str1.end())
        <= traits_inst.transform(str.begin(), str.end())
    && traits_inst.transform(str.begin(), str.end())
        <= traits_inst.transform(str2.begin(), str2.end());
  ```

- During matching of a regular expression finite state machine against a sequence of characters, testing whether a collating element is a member of a primary equivalence class is conducted by first converting the collating element and the equivalence class to sort keys using `traits::transform_primary`, and then comparing the sort keys for equality.

- During matching of a regular expression finite state machine against a sequence of characters, a character `c` is a member of a character class designated by an iterator range `[first, last)` if `traits_inst.isctype(c, traits_inst.lookup_classname(first, last, flags() & icase))` is `true`.

*See also:* ECMA-262 15.10
