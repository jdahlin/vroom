# 28.4.6.3.2 Virtual functions [locale.moneypunct.virtuals]

[Contents](../../README.md) · up: [28.4.6.3 Class template `moneypunct` \[locale.moneypunct\]](locale.moneypunct.md) · prev: [28.4.6.3.1 Members \[locale.moneypunct.members\]](locale.moneypunct.members.md) · next: [28.4.6.4 Class template `moneypunct_byname` \[locale.moneypunct.byname\]](locale.moneypunct.byname.md)

```cpp
charT do_decimal_point() const;
```

**1** *Returns:* The radix separator to use in case `do_frac_digits()` is greater than zero.[^1]

```cpp
charT do_thousands_sep() const;
```

**2** *Returns:* The digit group separator to use in case `do_grouping()` specifies a digit grouping pattern.[^2]

```cpp
string do_grouping() const;
```

**3** *Returns:* A pattern defined identically as, but not necessarily equal to, the result of `numpunct<charT>::do_grouping()`.[^3]

```cpp
string_type do_curr_symbol() const;
```

**4** *Returns:* A string to use as the currency identifier symbol. [*Note 1*: For specializations where the second template parameter is `true`, this is typically four characters long: a three-letter code as specified by ISO 4217 followed by a space. — *end note*]

```cpp
string_type do_positive_sign() const;
string_type do_negative_sign() const;
```

**5** *Returns:* `do_positive_sign()` returns the string to use to indicate a positive monetary value;[^4] `do_negative_sign()` returns the string to use to indicate a negative value.

```cpp
int do_frac_digits() const;
```

**6** *Returns:* The number of digits after the decimal radix separator, if any.[^5]

```cpp
pattern do_pos_format() const;
pattern do_neg_format() const;
```

**7** *Returns:* The specializations required in [Table 103](locale.category.md#tab:locale.spec) ([[locale.category]](locale.category.md)), namely

- `moneypunct<char>`,
- `moneypunct<wchar_t>`,
- `moneypunct<char, true>`, and
- `moneypunct<wchar_t, true>`,

return an object of type `pattern` initialized to `{ symbol, sign, none, value }`.[^6]

[^1]: In common U.S. locales this is `'.'`.
[^2]: In common U.S. locales this is `','`.
[^3]: To specify grouping by 3s, the value is `"\003"` *not* `"3"`.
[^4]: This is usually the empty string.
[^5]: In common U.S. locales, this is 2.
[^6]: Note that the international symbol returned by `do_curr_symbol()` usually contains a space, itself; for example, `"USD "`.
