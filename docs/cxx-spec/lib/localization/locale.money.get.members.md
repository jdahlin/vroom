# 28.4.6.1.1 Members [locale.money.get.members]

[Contents](../../README.md) · up: [28.4.6.1 Class template `money_get` \[locale.money.get\]](locale.money.get.md) · prev: [28.4.6.1 Class template `money_get` \[locale.money.get\]](locale.money.get.md) · next: [28.4.6.1.2 Virtual functions \[locale.money.get.virtuals\]](locale.money.get.virtuals.md)

```cpp
iter_type get(iter_type s, iter_type end, bool intl, ios_base& f,
              ios_base::iostate& err, long double& quant) const;
iter_type get(iter_type s, iter_type end, bool intl, ios_base& f,
              ios_base::iostate& err, string_type& quant) const;
```

**1** *Returns:* `do_get(s, end, intl, f, err, quant)`.
