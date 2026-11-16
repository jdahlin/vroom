# 20.7.6 Relational operators [variant.relops]

[Contents](../../README.md) · up: [20.7 Variants \[variant\]](variant.md) · prev: [20.7.5 Value access \[variant.get\]](variant.get.md) · next: [20.7.7 Visitation \[variant.visit\]](variant.visit.md)

```cpp
template<class... Types>
  constexpr bool operator==(const variant<Types...>& v, const variant<Types...>& w);
```

**1** *Mandates:* `get<i>(v) == get<i>(w)` is a valid expression that is convertible to `bool`, for all *i*.

**2** *Returns:* If `v.index() != w.index()`, `false`; otherwise if `v.valueless_by_exception()`, `true`; otherwise `get<i>(v) == get<i>(w)` with *i* being `v.index()`.

```cpp
template<class... Types>
  constexpr bool operator!=(const variant<Types...>& v, const variant<Types...>& w);
```

**3** *Mandates:* `get<i>(v) != get<i>(w)` is a valid expression that is convertible to `bool`, for all *i*.

**4** *Returns:* If `v.index() != w.index()`, `true`; otherwise if `v.valueless_by_exception()`, `false`; otherwise `get<i>(v) != get<i>(w)` with *i* being `v.index()`.

```cpp
template<class... Types>
  constexpr bool operator<(const variant<Types...>& v, const variant<Types...>& w);
```

**5** *Mandates:* `get<i>(v) < get<i>(w)` is a valid expression that is convertible to `bool`, for all *i*.

**6** *Returns:* If `w.valueless_by_exception()`, `false`; otherwise if `v.valueless_by_exception()`, `true`; otherwise, if `v.index() < w.index()`, `true`; otherwise if `v.index() > w.index()`, `false`; otherwise `get<i>(v) < get<i>(w)` with *i* being `v.index()`.

```cpp
template<class... Types>
  constexpr bool operator>(const variant<Types...>& v, const variant<Types...>& w);
```

**7** *Mandates:* `get<i>(v) > get<i>(w)` is a valid expression that is convertible to `bool`, for all *i*.

**8** *Returns:* If `v.valueless_by_exception()`, `false`; otherwise if `w.valueless_by_exception()`, `true`; otherwise, if `v.index() > w.index()`, `true`; otherwise if `v.index() < w.index()`, `false`; otherwise `get<i>(v) > get<i>(w)` with *i* being `v.index()`.

```cpp
template<class... Types>
  constexpr bool operator<=(const variant<Types...>& v, const variant<Types...>& w);
```

**9** *Mandates:* `get<i>(v) <= get<i>(w)` is a valid expression that is convertible to `bool`, for all *i*.

**10** *Returns:* If `v.valueless_by_exception()`, `true`; otherwise if `w.valueless_by_exception()`, `false`; otherwise, if `v.index() < w.index()`, `true`; otherwise if `v.index() > w.index()`, `false`; otherwise `get<i>(v) <= get<i>(w)` with *i* being `v.index()`.

```cpp
template<class... Types>
  constexpr bool operator>=(const variant<Types...>& v, const variant<Types...>& w);
```

**11** *Mandates:* `get<i>(v) >= get<i>(w)` is a valid expression that is convertible to `bool`, for all *i*.

**12** *Returns:* If `w.valueless_by_exception()`, `true`; otherwise if `v.valueless_by_exception()`, `false`; otherwise, if `v.index() > w.index()`, `true`; otherwise if `v.index() < w.index()`, `false`; otherwise `get<i>(v) >= get<i>(w)` with *i* being `v.index()`.

```cpp
template<class... Types> requires (three_way_comparable<Types> && ...)
  constexpr common_comparison_category_t<compare_three_way_result_t<Types>...>
    operator<=>(const variant<Types...>& v, const variant<Types...>& w);
```

**13** *Effects:* Equivalent to:

```cpp
if (v.valueless_by_exception() && w.valueless_by_exception())
  return strong_ordering::equal;
if (v.valueless_by_exception()) return strong_ordering::less;
if (w.valueless_by_exception()) return strong_ordering::greater;
if (auto c = v.index() <=> w.index(); c != 0) return c;
return get<i>(v) <=> get<i>(w);
```

with *i* being `v.index()`.
