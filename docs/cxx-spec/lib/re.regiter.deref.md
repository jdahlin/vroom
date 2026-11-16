# 30.12.1.3 Indirection [re.regiter.deref]

[Contents](../README.md) · up: [30.12.1 Class template `regex_iterator` \[re.regiter\]](re.regiter.md) · prev: [30.12.1.2 Comparisons \[re.regiter.comp\]](re.regiter.comp.md) · next: [30.12.1.4 Increment \[re.regiter.incr\]](re.regiter.incr.md)

```cpp
const value_type& operator*() const;
```

**1** *Returns:* `match`.

```cpp
const value_type* operator->() const;
```

**2** *Returns:* `addressof(match)`.
