# 20.14.9.2 Class template `logical_or` [logical.operations.or]

[Contents](../README.md) · up: [20.14.9 Logical operations \[logical.operations\]](logical.operations.md) · prev: [20.14.9.1 Class template `logical_and` \[logical.operations.and\]](logical.operations.and.md) · next: [20.14.9.3 Class template `logical_not` \[logical.operations.not\]](logical.operations.not.md)

```cpp
template<class T = void> struct logical_or {
  constexpr bool operator()(const T& x, const T& y) const;
};
```

```cpp
constexpr bool operator()(const T& x, const T& y) const;
```

**1** *Returns:* `x || y`.

```cpp
template<> struct logical_or<void> {
  template<class T, class U> constexpr auto operator()(T&& t, U&& u) const
    -> decltype(std::forward<T>(t) || std::forward<U>(u));

  using is_transparent = unspecified;
};
```

```cpp
template<class T, class U> constexpr auto operator()(T&& t, U&& u) const
    -> decltype(std::forward<T>(t) || std::forward<U>(u));
```

**2** *Returns:* `std::forward<T>(t) || std::forward<U>(u)`.
