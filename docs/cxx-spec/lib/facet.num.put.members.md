# 28.4.2.2.1 Members [facet.num.put.members]

[Contents](../README.md) · up: [28.4.2.2 Class template `num_put` \[locale.nm.put\]](locale.nm.put.md) · prev: [28.4.2.2 Class template `num_put` \[locale.nm.put\]](locale.nm.put.md) · next: [28.4.2.2.2 Virtual functions \[facet.num.put.virtuals\]](facet.num.put.virtuals.md)

```cpp
iter_type put(iter_type out, ios_base& str, char_type fill, bool val) const;
iter_type put(iter_type out, ios_base& str, char_type fill, long val) const;
iter_type put(iter_type out, ios_base& str, char_type fill, long long val) const;
iter_type put(iter_type out, ios_base& str, char_type fill, unsigned long val) const;
iter_type put(iter_type out, ios_base& str, char_type fill, unsigned long long val) const;
iter_type put(iter_type out, ios_base& str, char_type fill, double val) const;
iter_type put(iter_type out, ios_base& str, char_type fill, long double val) const;
iter_type put(iter_type out, ios_base& str, char_type fill, const void* val) const;
```

**1** *Returns:* `do_put(out, str, fill, val)`.
