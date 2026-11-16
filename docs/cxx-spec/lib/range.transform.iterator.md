# 24.7.5.3 Class template `transform_view::iterator` [range.transform.iterator]

[Contents](../README.md) · up: [24.7.5 Transform view \[range.transform\]](range.transform.md) · prev: [24.7.5.2 Class template `transform_view` \[range.transform.view\]](range.transform.view.md) · next: [24.7.5.4 Class template `transform_view::sentinel` \[range.transform.sentinel\]](range.transform.sentinel.md)

```cpp
namespace std::ranges {
  template<input_range V, copy_constructible F>
    requires view<V> && is_object_v<F> &&
             regular_invocable<F&, range_reference_t<V>> &&
             can-reference<invoke_result_t<F&, range_reference_t<V>>>
  template<bool Const>
  class transform_view<V, F>::iterator {
  private:
    using Parent =                              // exposition only
      conditional_t<Const, const transform_view, transform_view>;
    using Base   =                              // exposition only
      conditional_t<Const, const V, V>;
    iterator_t<Base> current_ =                 // exposition only
      iterator_t<Base>();
    Parent* parent_ = nullptr;                  // exposition only
  public:
    using iterator_concept  = see below;
    using iterator_category = see below;
    using value_type        =
      remove_cvref_t<invoke_result_t<F&, range_reference_t<Base>>>;
    using difference_type   = range_difference_t<Base>;

    iterator() = default;
    constexpr iterator(Parent& parent, iterator_t<Base> current);
    constexpr iterator(iterator<!Const> i)
      requires Const && convertible_to<iterator_t<V>, iterator_t<Base>>;

    constexpr iterator_t<Base> base() const &
      requires copyable<iterator_t<Base>>;
    constexpr iterator_t<Base> base() &&;
    constexpr decltype(auto) operator*() const
    { return invoke(*parent_->fun_, *current_); }

    constexpr iterator& operator++();
    constexpr void operator++(int);
    constexpr iterator operator++(int) requires forward_range<Base>;

    constexpr iterator& operator--() requires bidirectional_range<Base>;
    constexpr iterator operator--(int) requires bidirectional_range<Base>;

    constexpr iterator& operator+=(difference_type n)
      requires random_access_range<Base>;
    constexpr iterator& operator-=(difference_type n)
      requires random_access_range<Base>;
    constexpr decltype(auto) operator[](difference_type n) const
      requires random_access_range<Base>
    { return invoke(*parent_->fun_, current_[n]); }

    friend constexpr bool operator==(const iterator& x, const iterator& y)
      requires equality_comparable<iterator_t<Base>>;

    friend constexpr bool operator<(const iterator& x, const iterator& y)
      requires random_access_range<Base>;
    friend constexpr bool operator>(const iterator& x, const iterator& y)
      requires random_access_range<Base>;
    friend constexpr bool operator<=(const iterator& x, const iterator& y)
      requires random_access_range<Base>;
    friend constexpr bool operator>=(const iterator& x, const iterator& y)
      requires random_access_range<Base>;
    friend constexpr auto operator<=>(const iterator& x, const iterator& y)
      requires random_access_range<Base> && three_way_comparable<iterator_t<Base>>;

    friend constexpr iterator operator+(iterator i, difference_type n)
      requires random_access_range<Base>;
    friend constexpr iterator operator+(difference_type n, iterator i)
      requires random_access_range<Base>;

    friend constexpr iterator operator-(iterator i, difference_type n)
      requires random_access_range<Base>;
    friend constexpr difference_type operator-(const iterator& x, const iterator& y)
      requires random_access_range<Base>;

    friend constexpr decltype(auto) iter_move(const iterator& i)
      noexcept(noexcept(invoke(*i.parent_->fun_, *i.current_)))
    {
      if constexpr (is_lvalue_reference_v<decltype(*i)>)
        return std::move(*i);
      else
        return *i;
    }

    friend constexpr void iter_swap(const iterator& x, const iterator& y)
      noexcept(noexcept(ranges::iter_swap(x.current_, y.current_)))
      requires indirectly_swappable<iterator_t<Base>>;
  };
}
```

**1** `iterator::iterator_concept` is defined as follows:

- If `V` models `random_access_range`, then `iterator_concept` denotes `random_access_iterator_tag`.
- Otherwise, if `V` models `bidirectional_range`, then `iterator_concept` denotes `bidirectional_iterator_tag`.
- Otherwise, if `V` models `forward_range`, then `iterator_concept` denotes `forward_iterator_tag`.
- Otherwise, `iterator_concept` denotes `input_iterator_tag`.

**2** `iterator::iterator_category` is defined as follows: Let `C` denote the type `iterator_traits<iterator_t<Base>>::iterator_category`.

- If `is_lvalue_reference_v<invoke_result_t<F&, range_reference_t<Base>>>` is `true`, then

  - if `C` models `derived_from<contiguous_iterator_tag>`, `iterator_category` denotes `random_access_iterator_tag`;
  - otherwise, `iterator_category` denotes `C`.

- Otherwise, `iterator_category` denotes `input_iterator_tag`.

```cpp
constexpr iterator(Parent& parent, iterator_t<Base> current);
```

**3** *Effects:* Initializes `current_` with `std::move(current)` and `parent_` with `addressof(parent)`.

```cpp
constexpr iterator(iterator<!Const> i)
  requires Const && convertible_to<iterator_t<V>, iterator_t<Base>>;
```

**4** *Effects:* Initializes `current_` with `std::move(i.current_)` and `parent_` with `i.parent_`.

```cpp
constexpr iterator_t<Base> base() const &
  requires copyable<iterator_t<Base>>;
```

**5** *Effects:* Equivalent to: `return current_;`

```cpp
constexpr iterator_t<Base> base() &&;
```

**6** *Effects:* Equivalent to: `return std::move(current_);`

```cpp
constexpr iterator& operator++();
```

**7** *Effects:* Equivalent to:

```cpp
++current_;
return *this;
```

```cpp
constexpr void operator++(int);
```

**8** *Effects:* Equivalent to `++current_`.

```cpp
constexpr iterator operator++(int) requires forward_range<Base>;
```

**9** *Effects:* Equivalent to:

```cpp
auto tmp = *this;
++*this;
return tmp;
```

```cpp
constexpr iterator& operator--() requires bidirectional_range<Base>;
```

**10** *Effects:* Equivalent to:

```cpp
--current_;
return *this;
```

```cpp
constexpr iterator operator--(int) requires bidirectional_range<Base>;
```

**11** *Effects:* Equivalent to:

```cpp
auto tmp = *this;
--*this;
return tmp;
```

```cpp
constexpr iterator& operator+=(difference_type n)
  requires random_access_range<Base>;
```

**12** *Effects:* Equivalent to:

```cpp
current_ += n;
return *this;
```

```cpp
constexpr iterator& operator-=(difference_type n)
  requires random_access_range<Base>;
```

**13** *Effects:* Equivalent to:

```cpp
current_ -= n;
return *this;
```

```cpp
friend constexpr bool operator==(const iterator& x, const iterator& y)
  requires equality_comparable<iterator_t<Base>>;
```

**14** *Effects:* Equivalent to: `return x.current_ == y.current_;`

```cpp
friend constexpr bool operator<(const iterator& x, const iterator& y)
  requires random_access_range<Base>;
```

**15** *Effects:* Equivalent to: `return x.current_ < y.current_;`

```cpp
friend constexpr bool operator>(const iterator& x, const iterator& y)
  requires random_access_range<Base>;
```

**16** *Effects:* Equivalent to: `return y < x;`

```cpp
friend constexpr bool operator<=(const iterator& x, const iterator& y)
  requires random_access_range<Base>;
```

**17** *Effects:* Equivalent to: `return !(y < x);`

```cpp
friend constexpr bool operator>=(const iterator& x, const iterator& y)
  requires random_access_range<Base>;
```

**18** *Effects:* Equivalent to: `return !(x < y);`

```cpp
friend constexpr auto operator<=>(const iterator& x, const iterator& y)
  requires random_access_range<Base> && three_way_comparable<iterator_t<Base>>;
```

**19** *Effects:* Equivalent to: `return x.current_ <=> y.current_;`

```cpp
friend constexpr iterator operator+(iterator i, difference_type n)
  requires random_access_range<Base>;
friend constexpr iterator operator+(difference_type n, iterator i)
  requires random_access_range<Base>;
```

**20** *Effects:* Equivalent to: `return iterator{*i.parent_, i.current_ + n};`

```cpp
friend constexpr iterator operator-(iterator i, difference_type n)
  requires random_access_range<Base>;
```

**21** *Effects:* Equivalent to: `return iterator{*i.parent_, i.current_ - n};`

```cpp
friend constexpr difference_type operator-(const iterator& x, const iterator& y)
  requires random_access_range<Base>;
```

**22** *Effects:* Equivalent to: `return x.current_ - y.current_;`

```cpp
friend constexpr void iter_swap(const iterator& x, const iterator& y)
  noexcept(noexcept(ranges::iter_swap(x.current_, y.current_)))
  requires indirectly_swappable<iterator_t<Base>>;
```

**23** *Effects:* Equivalent to `ranges::iter_swap(x.current_, y.current_)`.
