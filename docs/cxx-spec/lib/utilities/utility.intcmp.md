# 20.2.7 Integer comparison functions [utility.intcmp]

[Contents](../../README.md) · up: [20.2 Utility components \[utility\]](utility.md) · prev: [20.2.6 Function template `declval` \[declval\]](declval.md) · next: [20.3 Compile-time integer sequences \[intseq\]](intseq.md)

```cpp
template<class T, class U>
  constexpr bool cmp_equal(T t, U u) noexcept;
```

**1** *Mandates:* Both `T` and `U` are standard integer types or extended integer types ([[basic.fundamental]](../../basic/basic.fundamental.md)).

**2** *Effects:* Equivalent to:

```cpp
using UT = make_unsigned_t<T>;
using UU = make_unsigned_t<U>;
if constexpr (is_signed_v<T> == is_signed_v<U>)
  return t == u;
else if constexpr (is_signed_v<T>)
  return t < 0 ? false : UT(t) == u;
else
  return u < 0 ? false : t == UU(u);
```

```cpp
template<class T, class U>
  constexpr bool cmp_not_equal(T t, U u) noexcept;
```

**3** *Effects:* Equivalent to: `return !cmp_equal(t, u);`

```cpp
template<class T, class U>
  constexpr bool cmp_less(T t, U u) noexcept;
```

**4** *Mandates:* Both `T` and `U` are standard integer types or extended integer types ([[basic.fundamental]](../../basic/basic.fundamental.md)).

**5** *Effects:* Equivalent to:

```cpp
using UT = make_unsigned_t<T>;
using UU = make_unsigned_t<U>;
if constexpr (is_signed_v<T> == is_signed_v<U>)
  return t < u;
else if constexpr (is_signed_v<T>)
  return t < 0 ? true : UT(t) < u;
else
  return u < 0 ? false : t < UU(u);
```

```cpp
template<class T, class U>
  constexpr bool cmp_greater(T t, U u) noexcept;
```

**6** *Effects:* Equivalent to: `return cmp_less(u, t);`

```cpp
template<class T, class U>
  constexpr bool cmp_less_equal(T t, U u) noexcept;
```

**7** *Effects:* Equivalent to: `return !cmp_greater(t, u);`

```cpp
template<class T, class U>
  constexpr bool cmp_greater_equal(T t, U u) noexcept;
```

**8** *Effects:* Equivalent to: `return !cmp_less(t, u);`

```cpp
template<class R, class T>
  constexpr bool in_range(T t) noexcept;
```

**9** *Mandates:* Both `T` and `R` are standard integer types or extended integer types ([[basic.fundamental]](../../basic/basic.fundamental.md)).

**10** *Effects:* Equivalent to:

```cpp
return cmp_greater_equal(t, numeric_limits<R>::min()) &&
       cmp_less_equal(t, numeric_limits<R>::max());
```

**11** [*Note 1*: These function templates cannot be used to compare `byte`, `char`, `char8_t`, `char16_t`, `char32_t`, `wchar_t`, and `bool`. — *end note*]
