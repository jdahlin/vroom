# 24.6.3.3 Class `iota_view::iterator` [range.iota.iterator]

[Contents](../../README.md) · up: [24.6.3 Iota view \[range.iota\]](range.iota.md) · prev: [24.6.3.2 Class template `iota_view` \[range.iota.view\]](range.iota.view.md) · next: [24.6.3.4 Class `iota_view::sentinel` \[range.iota.sentinel\]](range.iota.sentinel.md)

```cpp
namespace std::ranges {
  template<weakly_incrementable W, semiregular Bound>
    requires weakly-equality-comparable-with<W, Bound>
  struct iota_view<W, Bound>::iterator {
  private:
    W value_ = W();             // exposition only
  public:
    using iterator_concept = see below;
    using iterator_category = input_iterator_tag;
    using value_type = W;
    using difference_type = IOTA-DIFF-T(W);

    iterator() = default;
    constexpr explicit iterator(W value);

    constexpr W operator*() const noexcept(is_nothrow_copy_constructible_v<W>);

    constexpr iterator& operator++();
    constexpr void operator++(int);
    constexpr iterator operator++(int) requires incrementable<W>;

    constexpr iterator& operator--() requires decrementable<W>;
    constexpr iterator operator--(int) requires decrementable<W>;

    constexpr iterator& operator+=(difference_type n)
      requires advanceable<W>;
    constexpr iterator& operator-=(difference_type n)
      requires advanceable<W>;
    constexpr W operator[](difference_type n) const
      requires advanceable<W>;

    friend constexpr bool operator==(const iterator& x, const iterator& y)
      requires equality_comparable<W>;

    friend constexpr bool operator<(const iterator& x, const iterator& y)
      requires totally_ordered<W>;
    friend constexpr bool operator>(const iterator& x, const iterator& y)
      requires totally_ordered<W>;
    friend constexpr bool operator<=(const iterator& x, const iterator& y)
      requires totally_ordered<W>;
    friend constexpr bool operator>=(const iterator& x, const iterator& y)
      requires totally_ordered<W>;
    friend constexpr auto operator<=>(const iterator& x, const iterator& y)
      requires totally_ordered<W> && three_way_comparable<W>;

    friend constexpr iterator operator+(iterator i, difference_type n)
      requires advanceable<W>;
    friend constexpr iterator operator+(difference_type n, iterator i)
      requires advanceable<W>;

    friend constexpr iterator operator-(iterator i, difference_type n)
      requires advanceable<W>;
    friend constexpr difference_type operator-(const iterator& x, const iterator& y)
      requires advanceable<W>;
  };
}
```

**1** `iterator::iterator_concept` is defined as follows:

- If `W` models `advanceable`, then `iterator_concept` is `random_access_iterator_tag`.
- Otherwise, if `W` models `decrementable`, then `iterator_concept` is `bidirectional_iterator_tag`.
- Otherwise, if `W` models `incrementable`, then `iterator_concept` is `forward_iterator_tag`.
- Otherwise, `iterator_concept` is `input_iterator_tag`.

**2** [*Note 1*: Overloads for `iter_move` and `iter_swap` are omitted intentionally. — *end note*]

```cpp
constexpr explicit iterator(W value);
```

**3** *Effects:* Initializes `value_` with `value`.

```cpp
constexpr W operator*() const noexcept(is_nothrow_copy_constructible_v<W>);
```

**4** *Effects:* Equivalent to: `return value_;`

**5** [*Note 2*: The `noexcept` clause is needed by the default `iter_move` implementation. — *end note*]

```cpp
constexpr iterator& operator++();
```

**6** *Effects:* Equivalent to:

```cpp
++value_;
return *this;
```

```cpp
constexpr void operator++(int);
```

**7** *Effects:* Equivalent to `++*this`.

```cpp
constexpr iterator operator++(int) requires incrementable<W>;
```

**8** *Effects:* Equivalent to:

```cpp
auto tmp = *this;
++*this;
return tmp;
```

```cpp
constexpr iterator& operator--() requires decrementable<W>;
```

**9** *Effects:* Equivalent to:

```cpp
--value_;
return *this;
```

```cpp
constexpr iterator operator--(int) requires decrementable<W>;
```

**10** *Effects:* Equivalent to:

```cpp
auto tmp = *this;
--*this;
return tmp;
```

```cpp
constexpr iterator& operator+=(difference_type n)
  requires advanceable<W>;
```

**11** *Effects:* Equivalent to:

```cpp
if constexpr (is-integer-like<W> && !is-signed-integer-like<W>) {
  if (n >= difference_type(0))
    value_ += static_cast<W>(n);
  else
    value_ -= static_cast<W>(-n);
} else {
  value_ += n;
}
return *this;
```

```cpp
constexpr iterator& operator-=(difference_type n)
  requires advanceable<W>;
```

**12** *Effects:* Equivalent to:

```cpp
if constexpr (is-integer-like<W> && !is-signed-integer-like<W>) {
  if (n >= difference_type(0))
    value_ -= static_cast<W>(n);
  else
    value_ += static_cast<W>(-n);
} else {
  value_ -= n;
}
return *this;
```

```cpp
constexpr W operator[](difference_type n) const
  requires advanceable<W>;
```

**13** *Effects:* Equivalent to: `return W(value_ + n);`

```cpp
friend constexpr bool operator==(const iterator& x, const iterator& y)
  requires equality_comparable<W>;
```

**14** *Effects:* Equivalent to: `return x.value_ == y.value_;`

```cpp
friend constexpr bool operator<(const iterator& x, const iterator& y)
  requires totally_ordered<W>;
```

**15** *Effects:* Equivalent to: `return x.value_ < y.value_;`

```cpp
friend constexpr bool operator>(const iterator& x, const iterator& y)
  requires totally_ordered<W>;
```

**16** *Effects:* Equivalent to: `return y < x;`

```cpp
friend constexpr bool operator<=(const iterator& x, const iterator& y)
  requires totally_ordered<W>;
```

**17** *Effects:* Equivalent to: `return !(y < x);`

```cpp
friend constexpr bool operator>=(const iterator& x, const iterator& y)
  requires totally_ordered<W>;
```

**18** *Effects:* Equivalent to: `return !(x < y);`

```cpp
friend constexpr auto operator<=>(const iterator& x, const iterator& y)
  requires totally_ordered<W> && three_way_comparable<W>;
```

**19** *Effects:* Equivalent to: `return x.value_ <=> y.value_;`

```cpp
friend constexpr iterator operator+(iterator i, difference_type n)
  requires advanceable<W>;
```

**20** *Effects:* Equivalent to: `return i += n;`

```cpp
friend constexpr iterator operator+(difference_type n, iterator i)
  requires advanceable<W>;
```

**21** *Effects:* Equivalent to: `return i + n;`

```cpp
friend constexpr iterator operator-(iterator i, difference_type n)
  requires advanceable<W>;
```

**22** *Effects:* Equivalent to: `return i -= n;`

```cpp
friend constexpr difference_type operator-(const iterator& x, const iterator& y)
  requires advanceable<W>;
```

**23** *Effects:* Equivalent to:

```cpp
using D = difference_type;
if constexpr (is-integer-like<W>) {
  if constexpr (is-signed-integer-like<W>)
    return D(D(x.value_) - D(y.value_));
  else
    return (y.value_ > x.value_)
      ? D(-D(y.value_ - x.value_))
      : D(x.value_ - y.value_);
} else {
  return x.value_ - y.value_;
}
```
