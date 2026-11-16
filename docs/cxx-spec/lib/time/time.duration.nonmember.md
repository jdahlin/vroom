# 27.5.5 Non-member arithmetic [time.duration.nonmember]

[Contents](../../README.md) · up: [27.5 Class template `duration` \[time.duration\]](time.duration.md) · prev: [27.5.4 Special values \[time.duration.special\]](time.duration.special.md) · next: [27.5.6 Comparisons \[time.duration.comparisons\]](time.duration.comparisons.md)

**1** In the function descriptions that follow, unless stated otherwise, let `CD` represent the return type of the function.

```cpp
template<class Rep1, class Period1, class Rep2, class Period2>
  constexpr common_type_t<duration<Rep1, Period1>, duration<Rep2, Period2>>
    operator+(const duration<Rep1, Period1>& lhs, const duration<Rep2, Period2>& rhs);
```

**2** *Returns:* `CD(CD(lhs).count() + CD(rhs).count())`.

```cpp
template<class Rep1, class Period1, class Rep2, class Period2>
  constexpr common_type_t<duration<Rep1, Period1>, duration<Rep2, Period2>>
  operator-(const duration<Rep1, Period1>& lhs, const duration<Rep2, Period2>& rhs);
```

**3** *Returns:* `CD(CD(lhs).count() - CD(rhs).count())`.

```cpp
template<class Rep1, class Period, class Rep2>
  constexpr duration<common_type_t<Rep1, Rep2>, Period>
    operator*(const duration<Rep1, Period>& d, const Rep2& s);
```

**4** *Constraints:* `is_convertible_v<const Rep2&, common_type_t<Rep1, Rep2>>` is `true`.

**5** *Returns:* `CD(CD(d).count() * s)`.

```cpp
template<class Rep1, class Rep2, class Period>
  constexpr duration<common_type_t<Rep1, Rep2>, Period>
    operator*(const Rep1& s, const duration<Rep2, Period>& d);
```

**6** *Constraints:* `is_convertible_v<const Rep1&, common_type_t<Rep1, Rep2>>` is `true`.

**7** *Returns:* `d * s`.

```cpp
template<class Rep1, class Period, class Rep2>
  constexpr duration<common_type_t<Rep1, Rep2>, Period>
    operator/(const duration<Rep1, Period>& d, const Rep2& s);
```

**8** *Constraints:* `is_convertible_v<const Rep2&, common_type_t<Rep1, Rep2>>` is `true` and `Rep2` is not a specialization of `duration`.

**9** *Returns:* `CD(CD(d).count() / s)`.

```cpp
template<class Rep1, class Period1, class Rep2, class Period2>
  constexpr common_type_t<Rep1, Rep2>
    operator/(const duration<Rep1, Period1>& lhs, const duration<Rep2, Period2>& rhs);
```

**10** Let `CD` be `common_type_t<duration<Rep1, Period1>, duration<Rep2, Period2>>`.

**11** *Returns:* `CD(lhs).count() / CD(rhs).count()`.

```cpp
template<class Rep1, class Period, class Rep2>
  constexpr duration<common_type_t<Rep1, Rep2>, Period>
    operator%(const duration<Rep1, Period>& d, const Rep2& s);
```

**12** *Constraints:* `is_convertible_v<const Rep2&, common_type_t<Rep1, Rep2>>` is `true` and `Rep2` is not a specialization of `duration`.

**13** *Returns:* `CD(CD(d).count() % s)`.

```cpp
template<class Rep1, class Period1, class Rep2, class Period2>
  constexpr common_type_t<duration<Rep1, Period1>, duration<Rep2, Period2>>
    operator%(const duration<Rep1, Period1>& lhs, const duration<Rep2, Period2>& rhs);
```

**14** *Returns:* `CD(CD(lhs).count() % CD(rhs).count())`.
