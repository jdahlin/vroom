# 20.6.8 Comparison with `T` [optional.comp.with.t]

[Contents](../README.md) · up: [20.6 Optional objects \[optional\]](optional.md) · prev: [20.6.7 Comparison with `nullopt` \[optional.nullops\]](optional.nullops.md) · next: [20.6.9 Specialized algorithms \[optional.specalg\]](optional.specalg.md)

```cpp
template<class T, class U> constexpr bool operator==(const optional<T>& x, const U& v);
```

**1** *Mandates:* The expression `*x == v` is well-formed and its result is convertible to `bool`. [*Note 1*: `T` need not be *Cpp17EqualityComparable*. — *end note*]

**2** *Effects:* Equivalent to: `return bool(x) ? *x == v: false;`

```cpp
template<class T, class U> constexpr bool operator==(const T& v, const optional<U>& x);
```

**3** *Mandates:* The expression `v == *x` is well-formed and its result is convertible to `bool`.

**4** *Effects:* Equivalent to: `return bool(x) ? v == *x: false;`

```cpp
template<class T, class U> constexpr bool operator!=(const optional<T>& x, const U& v);
```

**5** *Mandates:* The expression `*x != v` is well-formed and its result is convertible to `bool`.

**6** *Effects:* Equivalent to: `return bool(x) ? *x != v: true;`

```cpp
template<class T, class U> constexpr bool operator!=(const T& v, const optional<U>& x);
```

**7** *Mandates:* The expression `v != *x` is well-formed and its result is convertible to `bool`.

**8** *Effects:* Equivalent to: `return bool(x) ? v != *x: true;`

```cpp
template<class T, class U> constexpr bool operator<(const optional<T>& x, const U& v);
```

**9** *Mandates:* The expression `*x < v` is well-formed and its result is convertible to `bool`.

**10** *Effects:* Equivalent to: `return bool(x) ? *x < v: true;`

```cpp
template<class T, class U> constexpr bool operator<(const T& v, const optional<U>& x);
```

**11** *Mandates:* The expression `v < *x` is well-formed and its result is convertible to `bool`.

**12** *Effects:* Equivalent to: `return bool(x) ? v < *x: false;`

```cpp
template<class T, class U> constexpr bool operator>(const optional<T>& x, const U& v);
```

**13** *Mandates:* The expression `*x > v` is well-formed and its result is convertible to `bool`.

**14** *Effects:* Equivalent to: `return bool(x) ? *x > v: false;`

```cpp
template<class T, class U> constexpr bool operator>(const T& v, const optional<U>& x);
```

**15** *Mandates:* The expression `v > *x` is well-formed and its result is convertible to `bool`.

**16** *Effects:* Equivalent to: `return bool(x) ? v > *x: true;`

```cpp
template<class T, class U> constexpr bool operator<=(const optional<T>& x, const U& v);
```

**17** *Mandates:* The expression `*x <= v` is well-formed and its result is convertible to `bool`.

**18** *Effects:* Equivalent to: `return bool(x) ? *x <= v: true;`

```cpp
template<class T, class U> constexpr bool operator<=(const T& v, const optional<U>& x);
```

**19** *Mandates:* The expression `v <= *x` is well-formed and its result is convertible to `bool`.

**20** *Effects:* Equivalent to: `return bool(x) ? v <= *x: false;`

```cpp
template<class T, class U> constexpr bool operator>=(const optional<T>& x, const U& v);
```

**21** *Mandates:* The expression `*x >= v` is well-formed and its result is convertible to `bool`.

**22** *Effects:* Equivalent to: `return bool(x) ? *x >= v: false;`

```cpp
template<class T, class U> constexpr bool operator>=(const T& v, const optional<U>& x);
```

**23** *Mandates:* The expression `v >= *x` is well-formed and its result is convertible to `bool`.

**24** *Effects:* Equivalent to: `return bool(x) ? v >= *x: true;`

```cpp
template<class T, three_way_comparable_with<T> U>
  constexpr compare_three_way_result_t<T,U>
    operator<=>(const optional<T>& x, const U& v);
```

**25** *Effects:* Equivalent to: `return bool(x) ? *x <=> v: strong_ordering::less;`
