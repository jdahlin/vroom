# 20.14.9.1 Class template `logical_and` [logical.operations.and]

[Contents](../../README.md) · up: [20.14.9 Logical operations \[logical.operations\]](logical.operations.md) · prev: [20.14.9 Logical operations \[logical.operations\]](logical.operations.md) · next: [20.14.9.2 Class template `logical_or` \[logical.operations.or\]](logical.operations.or.md)

```cpp
template<class T = void> struct logical_and {
  constexpr bool operator()(const T& x, const T& y) const;
};
```

```cpp
constexpr bool operator()(const T& x, const T& y) const;
```

**1** *Returns:* `x && y`.

```cpp
template<> struct logical_and<void> {
  template<class T, class U> constexpr auto operator()(T&& t, U&& u) const
    -> decltype(std::forward<T>(t) && std::forward<U>(u));

  using is_transparent = unspecified;
};
```

```cpp
template<class T, class U> constexpr auto operator()(T&& t, U&& u) const
    -> decltype(std::forward<T>(t) && std::forward<U>(u));
```

**2** *Returns:* `std::forward<T>(t) && std::forward<U>(u)`.
