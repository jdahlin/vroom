# 28.4.1.1.2 `ctype` virtual functions [locale.ctype.virtuals]

[Contents](../README.md) · up: [28.4.1.1 Class template `ctype` \[locale.ctype\]](locale.ctype.md) · prev: [28.4.1.1.1 `ctype` members \[locale.ctype.members\]](locale.ctype.members.md) · next: [28.4.1.2 Class template `ctype_byname` \[locale.ctype.byname\]](locale.ctype.byname.md)

```cpp
bool         do_is(mask m, charT c) const;
const charT* do_is(const charT* low, const charT* high, mask* vec) const;
```

**1** *Effects:* Classifies a character or sequence of characters. For each argument character, identifies a value `M` of type `ctype_base::mask`. The second form identifies a value `M` of type `ctype_base::mask` for each `*p` where `(low <= p && p < high)`, and places it into `vec[p - low]`.

**2** *Returns:* The first form returns the result of the expression `(M & m) != 0`; i.e., `true` if the character has the characteristics specified. The second form returns `high`.

```cpp
const charT* do_scan_is(mask m, const charT* low, const charT* high) const;
```

**3** *Effects:* Locates a character in a buffer that conforms to a classification `m`.

**4** *Returns:* The smallest pointer `p` in the range `[low, high)` such that `is(m, *p)` would return `true`; otherwise, returns `high`.

```cpp
const charT* do_scan_not(mask m, const charT* low, const charT* high) const;
```

**5** *Effects:* Locates a character in a buffer that fails to conform to a classification `m`.

**6** *Returns:* The smallest pointer `p`, if any, in the range `[low, high)` such that `is(m, *p)` would return `false`; otherwise, returns `high`.

```cpp
charT        do_toupper(charT c) const;
const charT* do_toupper(charT* low, const charT* high) const;
```

**7** *Effects:* Converts a character or characters to upper case. The second form replaces each character `*p` in the range `[low, high)` for which a corresponding upper-case character exists, with that character.

**8** *Returns:* The first form returns the corresponding upper-case character if it is known to exist, or its argument if not. The second form returns `high`.

```cpp
charT        do_tolower(charT c) const;
const charT* do_tolower(charT* low, const charT* high) const;
```

**9** *Effects:* Converts a character or characters to lower case. The second form replaces each character `*p` in the range `[low, high)` and for which a corresponding lower-case character exists, with that character.

**10** *Returns:* The first form returns the corresponding lower-case character if it is known to exist, or its argument if not. The second form returns `high`.

```cpp
charT        do_widen(char c) const;
const char*  do_widen(const char* low, const char* high, charT* dest) const;
```

**11** *Effects:* Applies the simplest reasonable transformation from a `char` value or sequence of `char` values to the corresponding `charT` value or values.[^1] The only characters for which unique transformations are required are those in the basic source character set ([[lex.charset]](../lex.charset.md)).

For any named `ctype` category with a `ctype <charT>` facet `ctc` and valid `ctype_base::mask` value `M`, `(ctc.is(M, c) || !is(M, do_widen(c)) )` is `true`.[^2]

The second form transforms each character `*p` in the range `[low, high)`, placing the result in `dest[p - low]`.

**12** *Returns:* The first form returns the transformed value. The second form returns `high`.

```cpp
char         do_narrow(charT c, char dfault) const;
const charT* do_narrow(const charT* low, const charT* high, char dfault, char* dest) const;
```

**13** *Effects:* Applies the simplest reasonable transformation from a `charT` value or sequence of `charT` values to the corresponding `char` value or values.

For any character `c` in the basic source character set ([[lex.charset]](../lex.charset.md)) the transformation is such that

```cpp
do_widen(do_narrow(c, 0)) == c
```

For any named `ctype` category with a `ctype<char>` facet `ctc` however, and `ctype_base::mask` value `M`,

```cpp
(is(M, c) || !ctc.is(M, do_narrow(c, dfault)) )
```

is `true` (unless `do_narrow` returns `dfault`). In addition, for any digit character `c`, the expression `(do_narrow(c, dfault) - '0')` evaluates to the digit value of the character. The second form transforms each character `*p` in the range `[low, high)`, placing the result (or `dfault` if no simple transformation is readily available) in `dest[p - low]`.

**14** *Returns:* The first form returns the transformed value; or `dfault` if no mapping is readily available. The second form returns `high`.

[^1]: The char argument of `do_widen` is intended to accept values derived from *character-literal*s for conversion to the locale’s encoding.
[^2]: In other words, the transformed character is not a member of any character classification that `c` is not also a member of.
