# 20.14.7.2 Class template `not_equal_to` [comparisons.not.equal.to]

[Contents](../README.md) · up: [20.14.7 Comparisons \[comparisons\]](comparisons.md) · prev: [20.14.7.1 Class template `equal_to` \[comparisons.equal.to\]](comparisons.equal.to.md) · next: [20.14.7.3 Class template `greater` \[comparisons.greater\]](comparisons.greater.md)

```cpp
template<class T = void> struct not_equal_to {
  constexpr bool operator()(const T& x, const T& y) const;
};
```

```cpp
constexpr bool operator()(const T& x, const T& y) const;
```

**1** *Returns:* `x != y`.

```cpp
template<> struct not_equal_to<void> {
  template<class T, class U> constexpr auto operator()(T&& t, U&& u) const
    -> decltype(std::forward<T>(t) != std::forward<U>(u));

  using is_transparent = unspecified;
};
```

```cpp
template<class T, class U> constexpr auto operator()(T&& t, U&& u) const
    -> decltype(std::forward<T>(t) != std::forward<U>(u));
```

**2** *Returns:* `std::forward<T>(t) != std::forward<U>(u)`.
