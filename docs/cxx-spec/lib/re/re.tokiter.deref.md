# 30.12.2.3 Indirection [re.tokiter.deref]

[Contents](../../README.md) · up: [30.12.2 Class template `regex_token_iterator` \[re.tokiter\]](re.tokiter.md) · prev: [30.12.2.2 Comparisons \[re.tokiter.comp\]](re.tokiter.comp.md) · next: [30.12.2.4 Increment \[re.tokiter.incr\]](re.tokiter.incr.md)

```cpp
const value_type& operator*() const;
```

**1** *Returns:* `*result`.

```cpp
const value_type* operator->() const;
```

**2** *Returns:* `result`.
