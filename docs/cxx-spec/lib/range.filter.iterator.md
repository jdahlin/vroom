# 24.7.4.3 Class `filter_view::iterator` [range.filter.iterator]

[Contents](../README.md) · up: [24.7.4 Filter view \[range.filter\]](range.filter.md) · prev: [24.7.4.2 Class template `filter_view` \[range.filter.view\]](range.filter.view.md) · next: [24.7.4.4 Class `filter_view::sentinel` \[range.filter.sentinel\]](range.filter.sentinel.md)

```cpp
namespace std::ranges {
  template<input_range V, indirect_unary_predicate<iterator_t<V>> Pred>
    requires view<V> && is_object_v<Pred>
  class filter_view<V, Pred>::iterator {
  private:
    iterator_t<V> current_ = iterator_t<V>();   // exposition only
    filter_view* parent_ = nullptr;             // exposition only
  public:
    using iterator_concept  = see below;
    using iterator_category = see below;
    using value_type        = range_value_t<V>;
    using difference_type   = range_difference_t<V>;

    iterator() = default;
    constexpr iterator(filter_view& parent, iterator_t<V> current);

    constexpr iterator_t<V> base() const &
      requires copyable<iterator_t<V>>;
    constexpr iterator_t<V> base() &&;
    constexpr range_reference_t<V> operator*() const;
    constexpr iterator_t<V> operator->() const
      requires has-arrow<iterator_t<V>> && copyable<iterator_t<V>>;

    constexpr iterator& operator++();
    constexpr void operator++(int);
    constexpr iterator operator++(int) requires forward_range<V>;

    constexpr iterator& operator--() requires bidirectional_range<V>;
    constexpr iterator operator--(int) requires bidirectional_range<V>;

    friend constexpr bool operator==(const iterator& x, const iterator& y)
      requires equality_comparable<iterator_t<V>>;

    friend constexpr range_rvalue_reference_t<V> iter_move(const iterator& i)
      noexcept(noexcept(ranges::iter_move(i.current_)));
    friend constexpr void iter_swap(const iterator& x, const iterator& y)
      noexcept(noexcept(ranges::iter_swap(x.current_, y.current_)))
      requires indirectly_swappable<iterator_t<V>>;
  };
}
```

**1** Modification of the element a `filter_view::iterator` denotes is permitted, but results in undefined behavior if the resulting value does not satisfy the filter predicate.

**2** `iterator::iterator_concept` is defined as follows:

- If `V` models `bidirectional_range`, then `iterator_concept` denotes `bidirectional_iterator_tag`.
- Otherwise, if `V` models `forward_range`, then `iterator_concept` denotes `forward_iterator_tag`.
- Otherwise, `iterator_concept` denotes `input_iterator_tag`.

**3** `iterator::iterator_category` is defined as follows:

- Let `C` denote the type `iterator_traits<iterator_t<V>>::iterator_category`.
- If `C` models `derived_from<bidirectional_iterator_tag>`, then `iterator_category` denotes `bidirectional_iterator_tag`.
- Otherwise, if `C` models `derived_from<forward_iterator_tag>`, then `iterator_category` denotes `forward_iterator_tag`.
- Otherwise, `iterator_category` denotes `C`.

```cpp
constexpr iterator(filter_view& parent, iterator_t<V> current);
```

**4** *Effects:* Initializes `current_` with `std::move(current)` and `parent_` with `addressof(parent)`.

```cpp
constexpr iterator_t<V> base() const &
  requires copyable<iterator_t<V>>;
```

**5** *Effects:* Equivalent to: `return current_;`

```cpp
constexpr iterator_t<V> base() &&;
```

**6** *Effects:* Equivalent to: `return std::move(current_);`

```cpp
constexpr range_reference_t<V> operator*() const;
```

**7** *Effects:* Equivalent to: `return *current_;`

```cpp
constexpr iterator_t<V> operator->() const
  requires has-arrow<iterator_t<V>> && copyable<iterator_t<V>>;
```

**8** *Effects:* Equivalent to: `return current_;`

```cpp
constexpr iterator& operator++();
```

**9** *Effects:* Equivalent to:

```cpp
current_ = ranges::find_if(std::move(++current_), ranges::end(parent_->base_),
                           ref(*parent_->pred_));
return *this;
```

```cpp
constexpr void operator++(int);
```

**10** *Effects:* Equivalent to `++*this`.

```cpp
constexpr iterator operator++(int) requires forward_range<V>;
```

**11** *Effects:* Equivalent to:

```cpp
auto tmp = *this;
++*this;
return tmp;
```

```cpp
constexpr iterator& operator--() requires bidirectional_range<V>;
```

**12** *Effects:* Equivalent to:

```cpp
do
  --current_;
while (!invoke(*parent_->pred_, *current_));
return *this;
```

```cpp
constexpr iterator operator--(int) requires bidirectional_range<V>;
```

**13** *Effects:* Equivalent to:

```cpp
auto tmp = *this;
--*this;
return tmp;
```

```cpp
friend constexpr bool operator==(const iterator& x, const iterator& y)
  requires equality_comparable<iterator_t<V>>;
```

**14** *Effects:* Equivalent to: `return x.current_ == y.current_;`

```cpp
friend constexpr range_rvalue_reference_t<V> iter_move(const iterator& i)
  noexcept(noexcept(ranges::iter_move(i.current_)));
```

**15** *Effects:* Equivalent to: `return ranges::iter_move(i.current_);`

```cpp
friend constexpr void iter_swap(const iterator& x, const iterator& y)
  noexcept(noexcept(ranges::iter_swap(x.current_, y.current_)))
  requires indirectly_swappable<iterator_t<V>>;
```

**16** *Effects:* Equivalent to `ranges::iter_swap(x.current_, y.current_)`.
