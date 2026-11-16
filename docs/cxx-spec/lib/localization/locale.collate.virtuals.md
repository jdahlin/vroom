# 28.4.4.1.2 Virtual functions [locale.collate.virtuals]

[Contents](../../README.md) · up: [28.4.4.1 Class template `collate` \[locale.collate\]](locale.collate.md) · prev: [28.4.4.1.1 Members \[locale.collate.members\]](locale.collate.members.md) · next: [28.4.4.2 Class template `collate_byname` \[locale.collate.byname\]](locale.collate.byname.md)

```cpp
int do_compare(const charT* low1, const charT* high1,
               const charT* low2, const charT* high2) const;
```

**1** *Returns:* `1` if the first string is greater than the second, `-1` if less, zero otherwise. The specializations required in [Table 102](locale.category.md#tab:locale.category.facets) ([[locale.category]](locale.category.md)), namely `collate<char>` and `collate<wchar_t>`, implement a lexicographical comparison ([[alg.lex.comparison]](../algorithms/alg.lex.comparison.md)).

```cpp
string_type do_transform(const charT* low, const charT* high) const;
```

**2** *Returns:* A `basic_string<charT>` value that, compared lexicographically with the result of calling `transform()` on another string, yields the same result as calling `do_compare()` on the same two strings.[^1]

```cpp
long do_hash(const charT* low, const charT* high) const;
```

**3** *Returns:* An integer value equal to the result of calling `hash()` on any other string for which `do_compare()` returns 0 (equal) when passed the two strings. [*Note 1*: The probability that the result equals that for another string which does not compare equal should be very small, approaching `(1.0/numeric_limits<unsigned long>::max())`. — *end note*]

[^1]: This function is useful when one string is being compared to many other strings.
