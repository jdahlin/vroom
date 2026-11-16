# 22.3.7.4 Specialized algorithms [array.special]

[Contents](../../README.md) · up: [22.3.7 Class template `array` \[array\]](array.md) · prev: [22.3.7.3 Member functions \[array.members\]](array.members.md) · next: [22.3.7.5 Zero-sized arrays \[array.zero\]](array.zero.md)

```cpp
template<class T, size_t N>
  constexpr void swap(array<T, N>& x, array<T, N>& y) noexcept(noexcept(x.swap(y)));
```

**1** *Constraints:* `N == 0` or `is_swappable_v<T>` is `true`.

**2** *Effects:* As if by `x.swap(y)`.

**3** *Complexity:* Linear in `N`.
