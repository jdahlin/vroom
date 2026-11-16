# 24.7.15.3 Class template `elements_view::iterator` [range.elements.iterator]

[Contents](../../README.md) · up: [24.7.15 Elements view \[range.elements\]](range.elements.md) · prev: [24.7.15.2 Class template `elements_view` \[range.elements.view\]](range.elements.view.md) · next: [24.7.15.4 Class template `elements_view::sentinel` \[range.elements.sentinel\]](range.elements.sentinel.md)

```cpp
namespace std::ranges {
  template<input_range V, size_t N>
    requires view<V> && has-tuple-element<range_value_t<V>, N> &&
             has-tuple-element<remove_reference_t<range_reference_t<V>>, N>
  template<bool Const>
  class elements_view<V, N>::iterator {                 // exposition only
    using Base = conditional_t<Const, const V, V>;      // exposition only

    iterator_t<Base> current_ = iterator_t<Base>();
  public:
    using iterator_category = typename iterator_traits<iterator_t<Base>>::iterator_category;
    using value_type = remove_cvref_t<tuple_element_t<N, range_value_t<Base>>>;
    using difference_type = range_difference_t<Base>;

    iterator() = default;
    constexpr explicit iterator(iterator_t<Base> current);
    constexpr iterator(iterator<!Const> i)
      requires Const && convertible_to<iterator_t<V>, iterator_t<Base>>;

    constexpr iterator_t<Base> base() const&
      requires copyable<iterator_t<Base>>;
    constexpr iterator_t<Base> base() &&;

    constexpr decltype(auto) operator*() const
    { return get<N>(*current_); }

    constexpr iterator& operator++();
    constexpr void operator++(int) requires (!forward_range<Base>);
    constexpr iterator operator++(int) requires forward_range<Base>;

    constexpr iterator& operator--() requires bidirectional_range<Base>;
    constexpr iterator operator--(int) requires bidirectional_range<Base>;

    constexpr iterator& operator+=(difference_type x)
      requires random_access_range<Base>;
    constexpr iterator& operator-=(difference_type x)
      requires random_access_range<Base>;

    constexpr decltype(auto) operator[](difference_type n) const
      requires random_access_range<Base>
    { return get<N>(*(current_ + n)); }

    friend constexpr bool operator==(const iterator& x, const iterator& y)
      requires equality_comparable<iterator_t<Base>>;

    friend constexpr bool operator<(const iterator& x, const iterator& y)
      requires random_access_range<Base>;
    friend constexpr bool operator>(const iterator& x, const iterator& y)
      requires random_access_range<Base>;
    friend constexpr bool operator<=(const iterator& y, const iterator& y)
      requires random_access_range<Base>;
    friend constexpr bool operator>=(const iterator& x, const iterator& y)
      requires random_access_range<Base>;
    friend constexpr auto operator<=>(const iterator& x, const iterator& y)
      requires random_access_range<Base> && three_way_comparable<iterator_t<Base>>;

    friend constexpr iterator operator+(const iterator& x, difference_type y)
      requires random_access_range<Base>;
    friend constexpr iterator operator+(difference_type x, const iterator& y)
      requires random_access_range<Base>;
    friend constexpr iterator operator-(const iterator& x, difference_type y)
      requires random_access_range<Base>;
    friend constexpr difference_type operator-(const iterator& x, const iterator& y)
      requires random_access_range<Base>;
  };
}
```

```cpp
constexpr explicit iterator(iterator_t<Base> current);
```

**1** *Effects:* Initializes `current_` with `std::move(current)`.

```cpp
constexpr iterator(iterator<!Const> i)
  requires Const && convertible_to<iterator_t<V>, iterator_t<Base>>;
```

**2** *Effects:* Initializes `current_` with `std::move(i.current_)`.

```cpp
constexpr iterator_t<Base> base() const&
  requires copyable<iterator_t<Base>>;
```

**3** *Effects:* Equivalent to: `return current_;`

```cpp
constexpr iterator_t<Base> base() &&;
```

**4** *Effects:* Equivalent to: `return std::move(current_);`

```cpp
constexpr iterator& operator++();
```

**5** *Effects:* Equivalent to:

```cpp
++current_;
return *this;
```

```cpp
constexpr void operator++(int) requires (!forward_range<Base>);
```

**6** *Effects:* Equivalent to: `++current_`.

```cpp
constexpr iterator operator++(int) requires forward_range<Base>;
```

**7** *Effects:* Equivalent to:

```cpp
auto temp = *this;
++current_;
return temp;
```

```cpp
constexpr iterator& operator--() requires bidirectional_range<Base>;
```

**8** *Effects:* Equivalent to:

```cpp
--current_;
return *this;
```

```cpp
constexpr iterator operator--(int) requires bidirectional_range<Base>;
```

**9** *Effects:* Equivalent to:

```cpp
auto temp = *this;
--current_;
return temp;
```

```cpp
constexpr iterator& operator+=(difference_type n);
  requires random_access_range<Base>;
```

**10** *Effects:* Equivalent to:

```cpp
current_ += n;
return *this;
```

```cpp
constexpr iterator& operator-=(difference_type n)
  requires random_access_range<Base>;
```

**11** *Effects:* Equivalent to:

```cpp
current_ -= n;
return *this;
```

```cpp
friend constexpr bool operator==(const iterator& x, const iterator& y)
  requires equality_comparable<Base>;
```

**12** *Effects:* Equivalent to: `return x.current_ == y.current_;`

```cpp
friend constexpr bool operator<(const iterator& x, const iterator& y)
  requires random_access_range<Base>;
```

**13** *Effects:* Equivalent to: `return x.current_ < y.current_;`

```cpp
friend constexpr bool operator>(const iterator& x, const iterator& y)
  requires random_access_range<Base>;
```

**14** *Effects:* Equivalent to: `return y < x;`

```cpp
friend constexpr bool operator<=(const iterator& x, const iterator& y)
  requires random_access_range<Base>;
```

**15** *Effects:* Equivalent to: `return !(y < x);`

```cpp
friend constexpr bool operator>=(const iterator& x, const iterator& y)
  requires random_access_range<Base>;
```

**16** *Effects:* Equivalent to: `return !(x < y);`

```cpp
friend constexpr auto operator<=>(const iterator& x, const iterator& y)
  requires random_access_range<Base> && three_way_comparable<iterator_t<Base>>;
```

**17** *Effects:* Equivalent to: `return x.current_ <=> y.current_;`

```cpp
friend constexpr iterator operator+(const iterator& x, difference_type y)
  requires random_access_range<Base>;
```

**18** *Effects:* Equivalent to: `return iterator{x} += y;`

```cpp
friend constexpr iterator operator+(difference_type x, const iterator& y)
  requires random_access_range<Base>;
```

**19** *Effects:* Equivalent to: `return y + x;`

```cpp
constexpr iterator operator-(const iterator& x, difference_type y)
  requires random_access_range<Base>;
```

**20** *Effects:* Equivalent to: `return iterator{x} -= y;`

```cpp
constexpr difference_type operator-(const iterator& x, const iterator& y)
  requires random_access_range<Base>;
```

**21** *Effects:* Equivalent to: `return x.current_ - y.current_;`
