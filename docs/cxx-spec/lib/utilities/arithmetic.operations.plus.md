# 20.14.6.1 Class template `plus` [arithmetic.operations.plus]

[Contents](../../README.md) · up: [20.14.6 Arithmetic operations \[arithmetic.operations\]](arithmetic.operations.md) · prev: [20.14.6 Arithmetic operations \[arithmetic.operations\]](arithmetic.operations.md) · next: [20.14.6.2 Class template `minus` \[arithmetic.operations.minus\]](arithmetic.operations.minus.md)

```cpp
template<class T = void> struct plus {
  constexpr T operator()(const T& x, const T& y) const;
};
```

```cpp
constexpr T operator()(const T& x, const T& y) const;
```

**1** *Returns:* `x + y`.

```cpp
template<> struct plus<void> {
  template<class T, class U> constexpr auto operator()(T&& t, U&& u) const
    -> decltype(std::forward<T>(t) + std::forward<U>(u));

  using is_transparent = unspecified;
};
```

```cpp
template<class T, class U> constexpr auto operator()(T&& t, U&& u) const
    -> decltype(std::forward<T>(t) + std::forward<U>(u));
```

**2** *Returns:* `std::forward<T>(t) + std::forward<U>(u)`.
