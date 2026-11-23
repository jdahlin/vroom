# 28.4.6.2.1 Members [locale.money.put.members]

[Contents](../../README.md) · up: [28.4.6.2 Class template `money_put` \[locale.money.put\]](locale.money.put.md) · prev: [28.4.6.2 Class template `money_put` \[locale.money.put\]](locale.money.put.md) · next: [28.4.6.2.2 Virtual functions \[locale.money.put.virtuals\]](locale.money.put.virtuals.md)

```cpp
iter_type put(iter_type s, bool intl, ios_base& f, char_type fill, long double quant) const;
iter_type put(iter_type s, bool intl, ios_base& f, char_type fill, const string_type& quant) const;
```

**1** *Returns:* `do_put(s, intl, f, loc, quant)`.
