# 21.3.3.2 Non-member comparison functions [string.cmp]

[Contents](../../README.md) · up: [21.3.3 Non-member functions \[string.nonmembers\]](string.nonmembers.md) · prev: [21.3.3.1 `operator+` \[string.op.plus\]](string.op.plus.md) · next: [21.3.3.3 `swap` \[string.special\]](string.special.md)

```cpp
template<class charT, class traits, class Allocator>
  constexpr bool
    operator==(const basic_string<charT, traits, Allocator>& lhs,
               const basic_string<charT, traits, Allocator>& rhs) noexcept;
template<class charT, class traits, class Allocator>
  constexpr bool operator==(const basic_string<charT, traits, Allocator>& lhs,
                            const charT* rhs);

template<class charT, class traits, class Allocator>
  constexpr see below operator<=>(const basic_string<charT, traits, Allocator>& lhs,
                                  const basic_string<charT, traits, Allocator>& rhs) noexcept;
template<class charT, class traits, class Allocator>
  constexpr see below operator<=>(const basic_string<charT, traits, Allocator>& lhs,
                                  const charT* rhs);
```

**1** *Effects:* Let `op` be the operator. Equivalent to:

```cpp
return basic_string_view<charT, traits>(lhs) op basic_string_view<charT, traits>(rhs);
```
