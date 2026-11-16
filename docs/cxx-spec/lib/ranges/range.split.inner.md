# 24.7.11.5 Class template `split_view::inner-iterator` [range.split.inner]

[Contents](../../README.md) · up: [24.7.11 Split view \[range.split\]](range.split.md) · prev: [24.7.11.4 Class `split_view::outer-iterator::value_type` \[range.split.outer.value\]](range.split.outer.value.md) · next: [24.7.12 Counted view \[range.counted\]](range.counted.md)

```cpp
namespace std::ranges {
  template<input_range V, forward_range Pattern>
    requires view<V> && view<Pattern> &&
             indirectly_comparable<iterator_t<V>, iterator_t<Pattern>, ranges::equal_to> &&
             (forward_range<V> || tiny-range<Pattern>)
  template<bool Const>
  struct split_view<V, Pattern>::inner-iterator {
  private:
    using Base = conditional_t<Const, const V, V>;      // exposition only
    outer-iterator<Const> i_ = outer-iterator<Const>(); // exposition only
    bool incremented_ = false;                          // exposition only
  public:
    using iterator_concept  = typename outer-iterator<Const>::iterator_concept;
    using iterator_category = see below;
    using value_type        = range_value_t<Base>;
    using difference_type   = range_difference_t<Base>;

    inner-iterator() = default;
    constexpr explicit inner-iterator(outer-iterator<Const> i);

    constexpr decltype(auto) operator*() const { return *i_.current; }

    constexpr inner-iterator& operator++();
    constexpr decltype(auto) operator++(int) {
      if constexpr (forward_range<V>) {
        auto tmp = *this;
        ++*this;
        return tmp;
      } else
        ++*this;
    }

    friend constexpr bool operator==(const inner-iterator& x, const inner-iterator& y)
      requires forward_range<Base>;

    friend constexpr bool operator==(const inner-iterator& x, default_sentinel_t);

    friend constexpr decltype(auto) iter_move(const inner-iterator& i)
    noexcept(noexcept(ranges::iter_move(i.i_.current))) {
      return ranges::iter_move(i.i_.current);
    }

    friend constexpr void iter_swap(const inner-iterator& x, const inner-iterator& y)
      noexcept(noexcept(ranges::iter_swap(x.i_.current, y.i_.current)))
      requires indirectly_swappable<iterator_t<Base>>;
  };
}
```

**1** The *typedef-name* `iterator_category` denotes:

- `forward_iterator_tag` if `iterator_traits<iterator_t<Base>>::iterator_category` models `derived_from<forward_iterator_tag>`;
- otherwise, `iterator_traits<iterator_t<Base>>::iterator_category`.

```cpp
constexpr explicit inner-iterator(outer-iterator<Const> i);
```

**2** *Effects:* Initializes `i_` with `std::move(i)`.

```cpp
constexpr inner-iterator& operator++();
```

**3** *Effects:* Equivalent to:

```cpp
incremented_ = true;
if constexpr (!forward_range<Base>) {
  if constexpr (Pattern::size() == 0) {
    return *this;
  }
}
++i_.current;
return *this;
```

```cpp
friend constexpr bool operator==(const inner-iterator& x, const inner-iterator& y)
  requires forward_range<Base>;
```

**4** *Effects:* Equivalent to: `return x.i_.current == y.i_.current;`

```cpp
friend constexpr bool operator==(const inner-iterator& x, default_sentinel_t);
```

**5** *Effects:* Equivalent to:

```cpp
auto [pcur, pend] = subrange{x.i_.parent_->pattern_};
auto end = ranges::end(x.i_.parent_->base_);
if constexpr (tiny-range<Pattern>) {
  const auto & cur = x.i_.current;
  if (cur == end) return true;
  if (pcur == pend) return x.incremented_;
  return *cur == *pcur;
} else {
  auto cur = x.i_.current;
  if (cur == end) return true;
  if (pcur == pend) return x.incremented_;
  do {
    if (*cur != *pcur) return false;
    if (++pcur == pend) return true;
  } while (++cur != end);
  return false;
}
```

```cpp
friend constexpr void iter_swap(const inner-iterator& x, const inner-iterator& y)
  noexcept(noexcept(ranges::iter_swap(x.i_.current, y.i_.current)))
  requires indirectly_swappable<iterator_t<Base>>;
```

**6** *Effects:* Equivalent to `ranges::iter_swap(x.i_.current, y.i_.current)`.
