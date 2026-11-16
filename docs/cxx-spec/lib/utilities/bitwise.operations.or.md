# 20.14.10.2 Class template `bit_or` [bitwise.operations.or]

[Contents](../../README.md) · up: [20.14.10 Bitwise operations \[bitwise.operations\]](bitwise.operations.md) · prev: [20.14.10.1 Class template `bit_and` \[bitwise.operations.and\]](bitwise.operations.and.md) · next: [20.14.10.3 Class template `bit_xor` \[bitwise.operations.xor\]](bitwise.operations.xor.md)

```cpp
template<class T = void> struct bit_or {
  constexpr T operator()(const T& x, const T& y) const;
};
```

```cpp
constexpr T operator()(const T& x, const T& y) const;
```

**1** *Returns:* `x | y`.

```cpp
template<> struct bit_or<void> {
  template<class T, class U> constexpr auto operator()(T&& t, U&& u) const
    -> decltype(std::forward<T>(t) | std::forward<U>(u));

  using is_transparent = unspecified;
};
```

```cpp
template<class T, class U> constexpr auto operator()(T&& t, U&& u) const
    -> decltype(std::forward<T>(t) | std::forward<U>(u));
```

**2** *Returns:* `std::forward<T>(t) | std::forward<U>(u)`.
