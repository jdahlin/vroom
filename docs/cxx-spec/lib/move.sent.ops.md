# 23.5.3.10 Operations [move.sent.ops]

[Contents](../README.md) · up: [23.5.3 Move iterators and sentinels \[move.iterators\]](move.iterators.md) · prev: [23.5.3.9 Class template `move_sentinel` \[move.sentinel\]](move.sentinel.md) · next: [23.5.4 Common iterators \[iterators.common\]](iterators.common.md)

```cpp
constexpr move_sentinel();
```

**1** *Effects:* Value-initializes `last`. If `is_trivially_default_constructible_v<S>` is `true`, then this constructor is a `constexpr` constructor.

```cpp
constexpr explicit move_sentinel(S s);
```

**2** *Effects:* Initializes `last` with `std::move(s)`.

```cpp
template<class S2>
  requires convertible_to<const S2&, S>
    constexpr move_sentinel(const move_sentinel<S2>& s);
```

**3** *Effects:* Initializes `last` with `s.last`.

```cpp
template<class S2>
  requires assignable_from<S&, const S2&>
    constexpr move_sentinel& operator=(const move_sentinel<S2>& s);
```

**4** *Effects:* Equivalent to: `last = s.last; return *this;`

```cpp
constexpr S base() const;
```

**5** *Returns:* `last`.
