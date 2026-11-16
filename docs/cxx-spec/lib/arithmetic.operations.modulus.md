# 20.14.6.5 Class template `modulus` [arithmetic.operations.modulus]

[Contents](../README.md) · up: [20.14.6 Arithmetic operations \[arithmetic.operations\]](arithmetic.operations.md) · prev: [20.14.6.4 Class template `divides` \[arithmetic.operations.divides\]](arithmetic.operations.divides.md) · next: [20.14.6.6 Class template `negate` \[arithmetic.operations.negate\]](arithmetic.operations.negate.md)

```cpp
template<class T = void> struct modulus {
  constexpr T operator()(const T& x, const T& y) const;
};
```

```cpp
constexpr T operator()(const T& x, const T& y) const;
```

**1** *Returns:* `x % y`.

```cpp
template<> struct modulus<void> {
  template<class T, class U> constexpr auto operator()(T&& t, U&& u) const
    -> decltype(std::forward<T>(t) % std::forward<U>(u));

  using is_transparent = unspecified;
};
```

```cpp
template<class T, class U> constexpr auto operator()(T&& t, U&& u) const
    -> decltype(std::forward<T>(t) % std::forward<U>(u));
```

**2** *Returns:* `std::forward<T>(t) % std::forward<U>(u)`.
