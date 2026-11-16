# 20.14.7.4 Class template `less` [comparisons.less]

[Contents](../README.md) · up: [20.14.7 Comparisons \[comparisons\]](comparisons.md) · prev: [20.14.7.3 Class template `greater` \[comparisons.greater\]](comparisons.greater.md) · next: [20.14.7.5 Class template `greater_equal` \[comparisons.greater.equal\]](comparisons.greater.equal.md)

```cpp
template<class T = void> struct less {
  constexpr bool operator()(const T& x, const T& y) const;
};
```

```cpp
constexpr bool operator()(const T& x, const T& y) const;
```

**1** *Returns:* `x < y`.

```cpp
template<> struct less<void> {
  template<class T, class U> constexpr auto operator()(T&& t, U&& u) const
    -> decltype(std::forward<T>(t) < std::forward<U>(u));

  using is_transparent = unspecified;
};
```

```cpp
template<class T, class U> constexpr auto operator()(T&& t, U&& u) const
    -> decltype(std::forward<T>(t) < std::forward<U>(u));
```

**2** *Returns:* `std::forward<T>(t) < std::forward<U>(u)`.
