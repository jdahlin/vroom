# 20.14.7.6 Class template `less_equal` [comparisons.less.equal]

[Contents](../../README.md) · up: [20.14.7 Comparisons \[comparisons\]](comparisons.md) · prev: [20.14.7.5 Class template `greater_equal` \[comparisons.greater.equal\]](comparisons.greater.equal.md) · next: [20.14.7.7 Class `compare_three_way` \[comparisons.three.way\]](comparisons.three.way.md)

```cpp
template<class T = void> struct less_equal {
  constexpr bool operator()(const T& x, const T& y) const;
};
```

```cpp
constexpr bool operator()(const T& x, const T& y) const;
```

**1** *Returns:* `x <= y`.

```cpp
template<> struct less_equal<void> {
  template<class T, class U> constexpr auto operator()(T&& t, U&& u) const
    -> decltype(std::forward<T>(t) <= std::forward<U>(u));

  using is_transparent = unspecified;
};
```

```cpp
template<class T, class U> constexpr auto operator()(T&& t, U&& u) const
    -> decltype(std::forward<T>(t) <= std::forward<U>(u));
```

**2** *Returns:* `std::forward<T>(t) <= std::forward<U>(u)`.
