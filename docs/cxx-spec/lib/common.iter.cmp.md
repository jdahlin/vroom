# 23.5.4.6 Comparisons [common.iter.cmp]

[Contents](../README.md) · up: [23.5.4 Common iterators \[iterators.common\]](iterators.common.md) · prev: [23.5.4.5 Navigation \[common.iter.nav\]](common.iter.nav.md) · next: [23.5.4.7 Customization \[common.iter.cust\]](common.iter.cust.md)

```cpp
template<class I2, sentinel_for<I> S2>
  requires sentinel_for<S, I2>
friend bool operator==(
  const common_iterator& x, const common_iterator<I2, S2>& y);
```

**1** *Preconditions:* `x.v_.valueless_by_exception()` and `y.v_.valueless_by_exception()` are each `false`.

**2** *Returns:* `true` if `i == j`, and otherwise `get<i>(x.v_) == get<j>(y.v_)`, where *i* is `x.v_.index()` and *j* is `y.v_.index()`.

```cpp
template<class I2, sentinel_for<I> S2>
  requires sentinel_for<S, I2> && equality_comparable_with<I, I2>
friend bool operator==(
  const common_iterator& x, const common_iterator<I2, S2>& y);
```

**3** *Preconditions:* `x.v_.valueless_by_exception()` and `y.v_.valueless_by_exception()` are each `false`.

**4** *Returns:* `true` if *i* and *j* are each `1`, and otherwise `get<i>(x.v_) == get<j>(y.v_)`, where *i* is `x.v_.index()` and *j* is `y.v_.index()`.

```cpp
template<sized_sentinel_for<I> I2, sized_sentinel_for<I> S2>
  requires sized_sentinel_for<S, I2>
friend iter_difference_t<I2> operator-(
  const common_iterator& x, const common_iterator<I2, S2>& y);
```

**5** *Preconditions:* `x.v_.valueless_by_exception()` and `y.v_.valueless_by_exception()` are each `false`.

**6** *Returns:* `0` if *i* and *j* are each `1`, and otherwise `get<i>(x.v_) - get<j>(y.v_)`, where *i* is `x.v_.index()` and *j* is `y.v_.index()`.
