# 28.4.6.3.1 Members [locale.moneypunct.members]

[Contents](../../README.md) · up: [28.4.6.3 Class template `moneypunct` \[locale.moneypunct\]](locale.moneypunct.md) · prev: [28.4.6.3 Class template `moneypunct` \[locale.moneypunct\]](locale.moneypunct.md) · next: [28.4.6.3.2 Virtual functions \[locale.moneypunct.virtuals\]](locale.moneypunct.virtuals.md)

```cpp
charT        decimal_point() const;
charT        thousands_sep() const;
string       grouping()      const;
string_type  curr_symbol()   const;
string_type  positive_sign() const;
string_type  negative_sign() const;
int          frac_digits()   const;
pattern      pos_format()    const;
pattern      neg_format()    const;
```

**1** Each of these functions `F` returns the result of calling the corresponding virtual member function `do_F()`.
