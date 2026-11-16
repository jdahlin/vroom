# 21.3.3.3 `swap` [string.special]

[Contents](../README.md) · up: [21.3.3 Non-member functions \[string.nonmembers\]](string.nonmembers.md) · prev: [21.3.3.2 Non-member comparison functions \[string.cmp\]](string.cmp.md) · next: [21.3.3.4 Inserters and extractors \[string.io\]](string.io.md)

```cpp
template<class charT, class traits, class Allocator>
  constexpr void
    swap(basic_string<charT, traits, Allocator>& lhs,
         basic_string<charT, traits, Allocator>& rhs)
      noexcept(noexcept(lhs.swap(rhs)));
```

**1** *Effects:* Equivalent to `lhs.swap(rhs)`.
