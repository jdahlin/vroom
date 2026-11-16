# 20.6.6 Relational operators [optional.relops]

[Contents](../README.md) · up: [20.6 Optional objects \[optional\]](optional.md) · prev: [20.6.5 Class `bad_optional_access` \[optional.bad.access\]](optional.bad.access.md) · next: [20.6.7 Comparison with `nullopt` \[optional.nullops\]](optional.nullops.md)

```cpp
template<class T, class U> constexpr bool operator==(const optional<T>& x, const optional<U>& y);
```

**1** *Mandates:* The expression `*x == *y` is well-formed and its result is convertible to `bool`. [*Note 1*: `T` need not be *Cpp17EqualityComparable*. — *end note*]

**2** *Returns:* If `bool(x) != bool(y)`, `false`; otherwise if `bool(x) == false`, `true`; otherwise `*x == *y`.

**3** *Remarks:* Specializations of this function template for which `*x == *y` is a core constant expression are constexpr functions.

```cpp
template<class T, class U> constexpr bool operator!=(const optional<T>& x, const optional<U>& y);
```

**4** *Mandates:* The expression `*x != *y` is well-formed and its result is convertible to `bool`.

**5** *Returns:* If `bool(x) != bool(y)`, `true`; otherwise, if `bool(x) == false`, `false`; otherwise `*x != *y`.

**6** *Remarks:* Specializations of this function template for which `*x != *y` is a core constant expression are constexpr functions.

```cpp
template<class T, class U> constexpr bool operator<(const optional<T>& x, const optional<U>& y);
```

**7** *Mandates:* `*x < *y` is well-formed and its result is convertible to `bool`.

**8** *Returns:* If `!y`, `false`; otherwise, if `!x`, `true`; otherwise `*x < *y`.

**9** *Remarks:* Specializations of this function template for which `*x < *y` is a core constant expression are constexpr functions.

```cpp
template<class T, class U> constexpr bool operator>(const optional<T>& x, const optional<U>& y);
```

**10** *Mandates:* The expression `*x > *y` is well-formed and its result is convertible to `bool`.

**11** *Returns:* If `!x`, `false`; otherwise, if `!y`, `true`; otherwise `*x > *y`.

**12** *Remarks:* Specializations of this function template for which `*x > *y` is a core constant expression are constexpr functions.

```cpp
template<class T, class U> constexpr bool operator<=(const optional<T>& x, const optional<U>& y);
```

**13** *Mandates:* The expression `*x <= *y` is well-formed and its result is convertible to `bool`.

**14** *Returns:* If `!x`, `true`; otherwise, if `!y`, `false`; otherwise `*x <= *y`.

**15** *Remarks:* Specializations of this function template for which `*x <= *y` is a core constant expression are constexpr functions.

```cpp
template<class T, class U> constexpr bool operator>=(const optional<T>& x, const optional<U>& y);
```

**16** *Mandates:* The expression `*x >= *y` is well-formed and its result is convertible to `bool`.

**17** *Returns:* If `!y`, `true`; otherwise, if `!x`, `false`; otherwise `*x >= *y`.

**18** *Remarks:* Specializations of this function template for which `*x >= *y` is a core constant expression are constexpr functions.

```cpp
template<class T, three_way_comparable_with<T> U>
  constexpr compare_three_way_result_t<T,U>
    operator<=>(const optional<T>& x, const optional<U>& y);
```

**19** *Returns:* If `x && y`, `*x <=> *y`; otherwise `bool(x) <=> bool(y)`.

**20** *Remarks:* Specializations of this function template for which `*x <=> *y` is a core constant expression are constexpr functions.
