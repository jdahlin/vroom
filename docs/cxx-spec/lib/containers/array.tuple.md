# 22.3.7.7 Tuple interface [array.tuple]

[Contents](../../README.md) · up: [22.3.7 Class template `array` \[array\]](array.md) · prev: [22.3.7.6 Array creation functions \[array.creation\]](array.creation.md) · next: [22.3.8 Class template `deque` \[deque\]](deque.md)

```cpp
template<class T, size_t N>
  struct tuple_size<array<T, N>> : integral_constant<size_t, N> { };
```

```cpp
template<size_t I, class T, size_t N>
  struct tuple_element<I, array<T, N>> {
    using type = T;
  };
```

**1** *Mandates:* `I < N` is `true`.

```cpp
template<size_t I, class T, size_t N>
  constexpr T& get(array<T, N>& a) noexcept;
template<size_t I, class T, size_t N>
  constexpr T&& get(array<T, N>&& a) noexcept;
template<size_t I, class T, size_t N>
  constexpr const T& get(const array<T, N>& a) noexcept;
template<size_t I, class T, size_t N>
  constexpr const T&& get(const array<T, N>&& a) noexcept;
```

**2** *Mandates:* `I < N` is `true`.

**3** *Returns:* A reference to the `I`<sup></sup>*th* element of `a`, where indexing is zero-based.
