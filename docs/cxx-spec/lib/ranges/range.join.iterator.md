# 24.7.10.3 Class template `join_view::iterator` [range.join.iterator]

[Contents](../../README.md) · up: [24.7.10 Join view \[range.join\]](range.join.md) · prev: [24.7.10.2 Class template `join_view` \[range.join.view\]](range.join.view.md) · next: [24.7.10.4 Class template `join_view::sentinel` \[range.join.sentinel\]](range.join.sentinel.md)

```cpp
namespace std::ranges {
  template<input_range V>
    requires view<V> && input_range<range_reference_t<V>> &&
             (is_reference_v<range_reference_t<V>> ||
              view<range_value_t<V>>)
  template<bool Const>
  struct join_view<V>::iterator {
  private:
    using Parent =                                              // exposition only
      conditional_t<Const, const join_view, join_view>;
    using Base   = conditional_t<Const, const V, V>;            // exposition only

    static constexpr bool ref-is-glvalue =                      // exposition only
      is_reference_v<range_reference_t<Base>>;

    iterator_t<Base> outer_ = iterator_t<Base>();               // exposition only
    iterator_t<range_reference_t<Base>> inner_ =                // exposition only
      iterator_t<range_reference_t<Base>>();
    Parent* parent_ = nullptr;                                  // exposition only

    constexpr void satisfy();                                   // exposition only
  public:
    using iterator_concept  = see below;
    using iterator_category = see below;
    using value_type        = range_value_t<range_reference_t<Base>>;
    using difference_type   = see below;

    iterator() = default;
    constexpr iterator(Parent& parent, iterator_t<Base> outer);
    constexpr iterator(iterator<!Const> i)
      requires Const &&
               convertible_to<iterator_t<V>, iterator_t<Base>> &&
               convertible_to<iterator_t<InnerRng>,
                              iterator_t<range_reference_t<Base>>>;

    constexpr decltype(auto) operator*() const { return *inner_; }

    constexpr iterator_t<Base> operator->() const
      requires has-arrow<iterator_t<Base>> && copyable<iterator_t<Base>>;

    constexpr iterator& operator++();
    constexpr void operator++(int);
    constexpr iterator operator++(int)
      requires ref-is-glvalue && forward_range<Base> &&
               forward_range<range_reference_t<Base>>;

    constexpr iterator& operator--()
      requires ref-is-glvalue && bidirectional_range<Base> &&
               bidirectional_range<range_reference_t<Base>> &&
               common_range<range_reference_t<Base>>;

    constexpr iterator operator--(int)
      requires ref-is-glvalue && bidirectional_range<Base> &&
               bidirectional_range<range_reference_t<Base>> &&
               common_range<range_reference_t<Base>>;

    friend constexpr bool operator==(const iterator& x, const iterator& y)
      requires ref-is-glvalue && equality_comparable<iterator_t<Base>> &&
               equality_comparable<iterator_t<range_reference_t<Base>>>;

    friend constexpr decltype(auto) iter_move(const iterator& i)
    noexcept(noexcept(ranges::iter_move(i.inner_))) {
      return ranges::iter_move(i.inner_);
    }

    friend constexpr void iter_swap(const iterator& x, const iterator& y)
      noexcept(noexcept(ranges::iter_swap(x.inner_, y.inner_)));
  };
}
```

**1** `iterator::iterator_concept` is defined as follows:

- If `ref-is-glvalue` is `true` and `Base` and `range_reference_t<Base>` each model `bidirectional_range`, then `iterator_concept` denotes `bidirectional_iterator_tag`.
- Otherwise, if `ref-is-glvalue` is `true` and `Base` and `range_reference_t<Base>` each model `forward_range`, then `iterator_concept` denotes `forward_iterator_tag`.
- Otherwise, `iterator_concept` denotes `input_iterator_tag`.

**2** `iterator::iterator_category` is defined as follows:

- Let *OUTERC* denote `iterator_traits<iterator_t<Base>>::iterator_category`, and let *INNERC* denote `iterator_traits<iterator_t<range_reference_t<Base>>>::iterator_category`.
- If `ref-is-glvalue` is `true` and *OUTERC* and *INNERC* each model `derived_from<bidirectional_iterator_tag>`, `iterator_category` denotes `bidirectional_iterator_tag`.
- Otherwise, if `ref-is-glvalue` is `true` and *OUTERC* and *INNERC* each model `derived_from<forward_iterator_tag>`, `iterator_category` denotes `forward_iterator_tag`.
- Otherwise, if *OUTERC* and *INNERC* each model `derived_from<input_iterator_tag>`, `iterator_category` denotes `input_iterator_tag`.
- Otherwise, `iterator_category` denotes `output_iterator_tag`.

**3** `iterator::difference_type` denotes the type:

```cpp
common_type_t<
  range_difference_t<Base>,
  range_difference_t<range_reference_t<Base>>>
```

**4** `join_view` iterators use the `satisfy` function to skip over empty inner ranges.

```cpp
constexpr void satisfy();       // exposition only
```

**5** *Effects:* Equivalent to:

```cpp
auto update_inner = [this](range_reference_t<Base> x) -> auto& {
  if constexpr (ref-is-glvalue) // x is a reference
    return x;
  else
    return (parent_->inner_ = views::all(std::move(x)));
};

for (; outer_ != ranges::end(parent_->base_); ++outer_) {
  auto& inner = update_inner(*outer_);
  inner_ = ranges::begin(inner);
  if (inner_ != ranges::end(inner))
    return;
}
if constexpr (ref-is-glvalue)
  inner_ = iterator_t<range_reference_t<Base>>();
```

```cpp
constexpr iterator(Parent& parent, iterator_t<Base> outer);
```

**6** *Effects:* Initializes `outer_` with `std::move(outer)` and `parent_` with `addressof(parent)`; then calls `satisfy()`.

```cpp
constexpr iterator(iterator<!Const> i)
  requires Const &&
           convertible_to<iterator_t<V>, iterator_t<Base>> &&
           convertible_to<iterator_t<InnerRng>,
                          iterator_t<range_reference_t<Base>>>;
```

**7** *Effects:* Initializes `outer_` with `std::move(i.outer_)`, `inner_` with `std::move(i.inner_)`, and `parent_` with `i.parent_`.

```cpp
constexpr iterator_t<Base> operator->() const
  requires has-arrow<iterator_t<Base>> && copyable<iterator_t<Base>>;
```

**8** *Effects:* Equivalent to `return inner_;`

```cpp
constexpr iterator& operator++();
```

**9** Let `inner-range` be:

- If `ref-is-glvalue` is `true`, `*outer_`.
- Otherwise, `parent_->inner_`.

**10** *Effects:* Equivalent to:

```cpp
auto&& inner_rng = inner-range;
if (++inner_ == ranges::end(inner_rng)) {
  ++outer_;
  satisfy();
}
return *this;
```

```cpp
constexpr void operator++(int);
```

**11** *Effects:* Equivalent to: `++*this`.

```cpp
constexpr iterator operator++(int)
  requires ref-is-glvalue && forward_range<Base> &&
           forward_range<range_reference_t<Base>>;
```

**12** *Effects:* Equivalent to:

```cpp
auto tmp = *this;
++*this;
return tmp;
```

```cpp
constexpr iterator& operator--()
  requires ref-is-glvalue && bidirectional_range<Base> &&
           bidirectional_range<range_reference_t<Base>> &&
           common_range<range_reference_t<Base>>;
```

**13** *Effects:* Equivalent to:

```cpp
if (outer_ == ranges::end(parent_->base_))
  inner_ = ranges::end(*--outer_);
while (inner_ == ranges::begin(*outer_))
  inner_ = ranges::end(*--outer_);
--inner_;
return *this;
```

```cpp
constexpr iterator operator--(int)
  requires ref-is-glvalue && bidirectional_range<Base> &&
           bidirectional_range<range_reference_t<Base>> &&
           common_range<range_reference_t<Base>>;
```

**14** *Effects:* Equivalent to:

```cpp
auto tmp = *this;
--*this;
return tmp;
```

```cpp
friend constexpr bool operator==(const iterator& x, const iterator& y)
  requires ref-is-glvalue && equality_comparable<iterator_t<Base>> &&
           equality_comparable<iterator_t<range_reference_t<Base>>>;
```

**15** *Effects:* Equivalent to: `return x.outer_ == y.outer_ && x.inner_ == y.inner_;`

```cpp
friend constexpr void iter_swap(const iterator& x, const iterator& y)
  noexcept(noexcept(ranges::iter_swap(x.inner_, y.inner_)));
```

**16** *Effects:* Equivalent to: `return ranges::iter_swap(x.inner_, y.inner_);`
