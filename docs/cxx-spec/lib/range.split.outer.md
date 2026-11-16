# 24.7.11.3 Class template `split_view::outer-iterator` [range.split.outer]

[Contents](../README.md) · up: [24.7.11 Split view \[range.split\]](range.split.md) · prev: [24.7.11.2 Class template `split_view` \[range.split.view\]](range.split.view.md) · next: [24.7.11.4 Class `split_view::outer-iterator::value_type` \[range.split.outer.value\]](range.split.outer.value.md)

```cpp
namespace std::ranges {
  template<input_range V, forward_range Pattern>
    requires view<V> && view<Pattern> &&
             indirectly_comparable<iterator_t<V>, iterator_t<Pattern>, ranges::equal_to> &&
             (forward_range<V> || tiny-range<Pattern>)
  template<bool Const>
  struct split_view<V, Pattern>::outer-iterator {
  private:
    using Parent =                          // exposition only
      conditional_t<Const, const split_view, split_view>;
    using Base   =                          // exposition only
      conditional_t<Const, const V, V>;
    Parent* parent_ = nullptr;              // exposition only
    iterator_t<Base> current_ =             // exposition only, present only if V models forward_range
      iterator_t<Base>();

  public:
    using iterator_concept  =
      conditional_t<forward_range<Base>, forward_iterator_tag, input_iterator_tag>;
    using iterator_category = input_iterator_tag;
    // [range.split.outer.value], class split_view::outer-iterator::value_type
    struct value_type;
    using difference_type   = range_difference_t<Base>;

    outer-iterator() = default;
    constexpr explicit outer-iterator(Parent& parent)
      requires (!forward_range<Base>);
    constexpr outer-iterator(Parent& parent, iterator_t<Base> current)
      requires forward_range<Base>;
    constexpr outer-iterator(outer-iterator<!Const> i)
      requires Const && convertible_to<iterator_t<V>, iterator_t<Base>>;

    constexpr value_type operator*() const;

    constexpr outer-iterator& operator++();
    constexpr decltype(auto) operator++(int) {
      if constexpr (forward_range<Base>) {
        auto tmp = *this;
        ++*this;
        return tmp;
      } else
        ++*this;
    }

    friend constexpr bool operator==(const outer-iterator& x, const outer-iterator& y)
      requires forward_range<Base>;

    friend constexpr bool operator==(const outer-iterator& x, default_sentinel_t);
  };
}
```

**1** Many of the following specifications refer to the notional member *current* of `outer-iterator`. *current* is equivalent to `current_` if `V` models `forward_range`, and `parent_->current_` otherwise.

```cpp
constexpr explicit outer-iterator(Parent& parent)
  requires (!forward_range<Base>);
```

**2** *Effects:* Initializes `parent_` with `addressof(parent)`.

```cpp
constexpr outer-iterator(Parent& parent, iterator_t<Base> current)
  requires forward_range<Base>;
```

**3** *Effects:* Initializes `parent_` with `addressof(parent)` and `current_` with `std::move(current)`.

```cpp
constexpr outer-iterator(outer-iterator<!Const> i)
  requires Const && convertible_to<iterator_t<V>, iterator_t<Base>>;
```

**4** *Effects:* Initializes `parent_` with `i.parent_` and `current_` with `std::move(i.current_)`.

```cpp
constexpr value_type operator*() const;
```

**5** *Effects:* Equivalent to: `return value_type{*this};`

```cpp
constexpr outer-iterator& operator++();
```

**6** *Effects:* Equivalent to:

```cpp
const auto end = ranges::end(parent_->base_);
if (current == end) return *this;
const auto [pbegin, pend] = subrange{parent_->pattern_};
if (pbegin == pend) ++current;
else {
  do {
    auto [b, p] = ranges::mismatch(std::move(current), end, pbegin, pend);
    current = std::move(b);
    if (p == pend) {
      break;            // The pattern matched; skip it
    }
  } while (++current != end);
}
return *this;
```

```cpp
friend constexpr bool operator==(const outer-iterator& x, const outer-iterator& y)
  requires forward_range<Base>;
```

**7** *Effects:* Equivalent to: `return x.current_ == y.current_;`

```cpp
friend constexpr bool operator==(const outer-iterator& x, default_sentinel_t);
```

**8** *Effects:* Equivalent to: `return x.current == ranges::end(x.parent_->base_);`
