# 20.14.6.6 Class template `negate` [arithmetic.operations.negate]

[Contents](../../README.md) · up: [20.14.6 Arithmetic operations \[arithmetic.operations\]](arithmetic.operations.md) · prev: [20.14.6.5 Class template `modulus` \[arithmetic.operations.modulus\]](arithmetic.operations.modulus.md) · next: [20.14.7 Comparisons \[comparisons\]](comparisons.md)

```cpp
template<class T = void> struct negate {
  constexpr T operator()(const T& x) const;
};
```

```cpp
constexpr T operator()(const T& x) const;
```

**1** *Returns:* `-x`.

```cpp
template<> struct negate<void> {
  template<class T> constexpr auto operator()(T&& t) const
    -> decltype(-std::forward<T>(t));

  using is_transparent = unspecified;
};
```

```cpp
template<class T> constexpr auto operator()(T&& t) const
    -> decltype(-std::forward<T>(t));
```

**2** *Returns:* `-std::forward<T>(t)`.
