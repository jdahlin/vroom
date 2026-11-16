# 30.12.2.2 Comparisons [re.tokiter.comp]

[Contents](../../README.md) · up: [30.12.2 Class template `regex_token_iterator` \[re.tokiter\]](re.tokiter.md) · prev: [30.12.2.1 Constructors \[re.tokiter.cnstr\]](re.tokiter.cnstr.md) · next: [30.12.2.3 Indirection \[re.tokiter.deref\]](re.tokiter.deref.md)

```cpp
bool operator==(const regex_token_iterator& right) const;
```

**1** *Returns:* `true` if `*this` and `right` are both end-of-sequence iterators, or if `*this` and `right` are both suffix iterators and `suffix == right.suffix`; otherwise returns `false` if `*this` or `right` is an end-of-sequence iterator or a suffix iterator. Otherwise returns `true` if `position == right.position`, `N == right.N`, and `subs == right.subs`. Otherwise returns `false`.
