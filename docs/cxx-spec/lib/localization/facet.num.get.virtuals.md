# 28.4.2.1.2 Virtual functions [facet.num.get.virtuals]

[Contents](../../README.md) · up: [28.4.2.1 Class template `num_get` \[locale.num.get\]](locale.num.get.md) · prev: [28.4.2.1.1 Members \[facet.num.get.members\]](facet.num.get.members.md) · next: [28.4.2.2 Class template `num_put` \[locale.nm.put\]](locale.nm.put.md)

```cpp
iter_type do_get(iter_type in, iter_type end, ios_base& str,
                ios_base::iostate& err, long& val) const;
iter_type do_get(iter_type in, iter_type end, ios_base& str,
                ios_base::iostate& err, long long& val) const;
iter_type do_get(iter_type in, iter_type end, ios_base& str,
                ios_base::iostate& err, unsigned short& val) const;
iter_type do_get(iter_type in, iter_type end, ios_base& str,
                ios_base::iostate& err, unsigned int& val) const;
iter_type do_get(iter_type in, iter_type end, ios_base& str,
                ios_base::iostate& err, unsigned long& val) const;
iter_type do_get(iter_type in, iter_type end, ios_base& str,
                ios_base::iostate& err, unsigned long long& val) const;
iter_type do_get(iter_type in, iter_type end, ios_base& str,
                ios_base::iostate& err, float& val) const;
iter_type do_get(iter_type in, iter_type end, ios_base& str,
                ios_base::iostate& err, double& val) const;
iter_type do_get(iter_type in, iter_type end, ios_base& str,
                ios_base::iostate& err, long double& val) const;
iter_type do_get(iter_type in, iter_type end, ios_base& str,
                ios_base::iostate& err, void*& val) const;
```

**1** *Effects:* Reads characters from `in`, interpreting them according to `str.flags()`, `use_facet<ctype<charT>>(loc)`, and `use_facet<numpunct<charT>>(loc)`, where `loc` is `str.getloc()`.

**2** The details of this operation occur in three stages

- Stage 1: Determine a conversion specifier
- Stage 2: Extract characters from `in` and determine a corresponding `char` value for the format expected by the conversion specification determined in stage 1.
- Stage 3: Store results

**3** The details of the stages are presented below.

- **Stage 1:** The function initializes local variables via

  ```cpp
  fmtflags flags = str.flags();
  fmtflags basefield = (flags & ios_base::basefield);
  fmtflags uppercase = (flags & ios_base::uppercase);
  fmtflags boolalpha = (flags & ios_base::boolalpha);
  ```

  For conversion to an integral type, the function determines the integral conversion specifier as indicated in [Table 106](facet.num.get.virtuals.md#tab:facet.num.get.int). The table is ordered. That is, the first line whose condition is true applies.

  <a id="tab:facet.num.get.int"></a>
  **Table 106: Integer conversions** [tab:facet.num.get.int]

  | State | `stdio` equivalent |
  | --- | --- |
  | `basefield == oct` | `%o` |
  | `basefield == hex` | `%X` |
  | `basefield == 0` | `%i` |
  | `signed` integral type | `%d` |
  | `unsigned` integral type | `%u` |

  For conversions to a floating-point type the specifier is `%g`.

  For conversions to `void*` the specifier is `%p`.

  A length modifier is added to the conversion specification, if needed, as indicated in [Table 107](facet.num.get.virtuals.md#tab:facet.num.get.length).

  <a id="tab:facet.num.get.length"></a>
  **Table 107: Length modifier** [tab:facet.num.get.length]

  | Type | Length modifier |
  | --- | --- |
  | `short` | `h` |
  | `unsigned short` | `h` |
  | `long` | `l` |
  | `unsigned long` | `l` |
  | `long long` | `ll` |
  | `unsigned long long` | `ll` |
  | `double` | `l` |
  | `long double` | `L` |

- **Stage 2:** If `in == end` then stage 2 terminates. Otherwise a `charT` is taken from `in` and local variables are initialized as if by

  ```cpp
  char_type ct = *in;
  char c = src[find(atoms, atoms + sizeof(src) - 1, ct) - atoms];
  if (ct == use_facet<numpunct<charT>>(loc).decimal_point())
  c = '.';
  bool discard =
    ct == use_facet<numpunct<charT>>(loc).thousands_sep()
    && use_facet<numpunct<charT>>(loc).grouping().length() != 0;
  ```

  where the values `src` and `atoms` are defined as if by:

  ```cpp
  static const char src[] = "0123456789abcdefxABCDEFX+-";
  char_type atoms[sizeof(src)];
  use_facet<ctype<charT>>(loc).widen(src, src + sizeof(src), atoms);
  ```

  for this value of `loc`.

  If `discard` is `true`, then if `'.'` has not yet been accumulated, then the position of the character is remembered, but the character is otherwise ignored. Otherwise, if `'.'` has already been accumulated, the character is discarded and Stage 2 terminates. If it is not discarded, then a check is made to determine if `c` is allowed as the next character of an input field of the conversion specifier returned by Stage 1. If so, it is accumulated.

  If the character is either discarded or accumulated then `in` is advanced by `++in` and processing returns to the beginning of stage 2.

- **Stage 3:** The sequence of `char`s accumulated in stage 2 (the field) is converted to a numeric value by the rules of one of the functions declared in the header `<cstdlib>`:

  - For a signed integer value, the function `strtoll`.
  - For an unsigned integer value, the function `strtoull`.
  - For a `float` value, the function `strtof`.
  - For a `double` value, the function `strtod`.
  - For a `long double` value, the function `strtold`.

  The numeric value to be stored can be one of:

  - zero, if the conversion function does not convert the entire field.
  - the most positive (or negative) representable value, if the field to be converted to a signed integer type represents a value too large positive (or negative) to be represented in `val`.
  - the most positive representable value, if the field to be converted to an unsigned integer type represents a value that cannot be represented in `val`.
  - the converted value, otherwise.

  The resultant numeric value is stored in `val`. If the conversion function does not convert the entire field, or if the field represents a value outside the range of representable values, `ios_base::failbit` is assigned to `err`.

**4** Digit grouping is checked. That is, the positions of discarded separators is examined for consistency with `use_facet<numpunct<charT>>(loc).grouping()`. If they are not consistent then `ios_base::failbit` is assigned to `err`.

**5** In any case, if stage 2 processing was terminated by the test for `in == end` then `err |= ios_base::eofbit` is performed.

```cpp
iter_type do_get(iter_type in, iter_type end, ios_base& str,
                 ios_base::iostate& err, bool& val) const;
```

**6** *Effects:* If `(str.flags()&ios_base::boolalpha) == 0` then input proceeds as it would for a `long` except that if a value is being stored into `val`, the value is determined according to the following: If the value to be stored is 0 then `false` is stored. If the value is `1` then `true` is stored. Otherwise `true` is stored and `ios_base::failbit` is assigned to `err`.

**7** Otherwise target sequences are determined “as if” by calling the members `falsename()` and `truename()` of the facet obtained by `use_facet<numpunct<charT>>(str.getloc())`. Successive characters in the range `[in, end)` (see [[sequence.reqmts]](../containers/sequence.reqmts.md)) are obtained and matched against corresponding positions in the target sequences only as necessary to identify a unique match. The input iterator `in` is compared to `end` only when necessary to obtain a character. If a target sequence is uniquely matched, `val` is set to the corresponding value. Otherwise `false` is stored and `ios_base::failbit` is assigned to `err`.

**8** The `in` iterator is always left pointing one position beyond the last character successfully matched. If `val` is set, then `err` is set to `str.goodbit`; or to `str.eofbit` if, when seeking another character to match, it is found that `(in == end)`. If `val` is not set, then `err` is set to `str.failbit`; or to `(str.failbit|str.eofbit)` if the reason for the failure was that `(in == end)`. [*Example 1*: For targets `true`: `"a"` and `false`: `"abb"`, the input sequence `"a"` yields `val == true` and `err == str.eofbit`; the input sequence `"abc"` yields `err = str.failbit`, with `in` ending at the `'c'` element. For targets `true`: `"1"` and `false`: `"0"`, the input sequence `"1"` yields `val == true` and `err == str.goodbit`. For empty targets `("")`, any input sequence yields `err == str.failbit`. — *end example*]

**9** *Returns:* `in`.
