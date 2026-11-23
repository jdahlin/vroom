# 28.4.3.1.1 Members [facet.numpunct.members]

[Contents](../../README.md) · up: [28.4.3.1 Class template `numpunct` \[locale.numpunct\]](locale.numpunct.md) · prev: [28.4.3.1 Class template `numpunct` \[locale.numpunct\]](locale.numpunct.md) · next: [28.4.3.1.2 Virtual functions \[facet.numpunct.virtuals\]](facet.numpunct.virtuals.md)

```cpp
char_type decimal_point() const;
```

**1** *Returns:* `do_decimal_point()`.

```cpp
char_type thousands_sep() const;
```

**2** *Returns:* `do_thousands_sep()`.

```cpp
string grouping()  const;
```

**3** *Returns:* `do_grouping()`.

```cpp
string_type truename()  const;
string_type falsename() const;
```

**4** *Returns:* `do_truename()` or `do_falsename()`, respectively.
