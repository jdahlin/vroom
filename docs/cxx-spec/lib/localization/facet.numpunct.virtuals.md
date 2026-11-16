# 28.4.3.1.2 Virtual functions [facet.numpunct.virtuals]

[Contents](../../README.md) · up: [28.4.3.1 Class template `numpunct` \[locale.numpunct\]](locale.numpunct.md) · prev: [28.4.3.1.1 Members \[facet.numpunct.members\]](facet.numpunct.members.md) · next: [28.4.3.2 Class template `numpunct_byname` \[locale.numpunct.byname\]](locale.numpunct.byname.md)

```cpp
char_type do_decimal_point() const;
```

**1** *Returns:* A character for use as the decimal radix separator. The required specializations return `'.'` or `L'.'`.

```cpp
char_type do_thousands_sep() const;
```

**2** *Returns:* A character for use as the digit group separator. The required specializations return `','` or `L','`.

```cpp
string do_grouping() const;
```

**3** *Returns:* A `string` `vec` used as a vector of integer values, in which each element `vec[i]` represents the number of digits[^1] in the group at position `i`, starting with position 0 as the rightmost group. If `vec.size() <= i`, the number is the same as group `(i - 1)`; if `(i < 0 || vec[i] <= 0 || vec[i] == CHAR_MAX)`, the size of the digit group is unlimited.

**4** The required specializations return the empty string, indicating no grouping.

```cpp
string_type do_truename()  const;
string_type do_falsename() const;
```

**5** *Returns:* A string representing the name of the boolean value `true` or `false`, respectively.

**6** In the base class implementation these names are `"true"` and `"false"`, or `L"true"` and `L"false"`.

[^1]: Thus, the string `"\003"` specifies groups of 3 digits each, and `"3"` probably indicates groups of 51 (!) digits each, because 51 is the ASCII value of `"3"`.
