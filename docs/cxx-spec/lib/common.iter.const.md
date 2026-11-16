# 23.5.4.3 Constructors and conversions [common.iter.const]

[Contents](../README.md) · up: [23.5.4 Common iterators \[iterators.common\]](iterators.common.md) · prev: [23.5.4.2 Associated types \[common.iter.types\]](common.iter.types.md) · next: [23.5.4.4 Accessors \[common.iter.access\]](common.iter.access.md)

```cpp
constexpr common_iterator(I i);
```

**1** *Effects:* Initializes `v_` as if by `v_{in_place_type<I>, std::move(i)}`.

```cpp
constexpr common_iterator(S s);
```

**2** *Effects:* Initializes `v_` as if by `v_{in_place_type<S>, std::move(s)}`.

```cpp
template<class I2, class S2>
  requires convertible_to<const I2&, I> && convertible_to<const S2&, S>
    constexpr common_iterator(const common_iterator<I2, S2>& x);
```

**3** *Preconditions:* `x.v_.valueless_by_exception()` is `false`.

**4** *Effects:* Initializes `v_` as if by `v_{in_place_index<i>, get<i>(x.v_)}`, where *i* is `x.v_.index()`.

```cpp
template<class I2, class S2>
  requires convertible_to<const I2&, I> && convertible_to<const S2&, S> &&
           assignable_from<I&, const I2&> && assignable_from<S&, const S2&>
    common_iterator& operator=(const common_iterator<I2, S2>& x);
```

**5** *Preconditions:* `x.v_.valueless_by_exception()` is `false`.

**6** *Effects:* Equivalent to:

- If `v_.index() == x.v_.index()`, then `get<i>(v_) = get<i>(x.v_)`.
- Otherwise, `v_.emplace<i>(get<i>(x.v_))`.

where *i* is `x.v_.index()`.

**7** *Returns:* `*this`
