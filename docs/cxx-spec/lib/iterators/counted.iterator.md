# 23.5.6.1 Class template `counted_iterator` [counted.iterator]

[Contents](../../README.md) · up: [23.5.6 Counted iterators \[iterators.counted\]](iterators.counted.md) · prev: [23.5.6 Counted iterators \[iterators.counted\]](iterators.counted.md) · next: [23.5.6.2 Constructors and conversions \[counted.iter.const\]](counted.iter.const.md)

**1** Class template `counted_iterator` is an iterator adaptor with the same behavior as the underlying iterator except that it keeps track of the distance to the end of its range. It can be used together with `default_sentinel` in calls to generic algorithms to operate on a range of *N* elements starting at a given position without needing to know the end position a priori.

**2** [*Example 1*:

```cpp
list<string> s;
// populate the list s with at least 10 strings
vector<string> v;
// copies 10 strings into v:
ranges::copy(counted_iterator(s.begin(), 10), default_sentinel, back_inserter(v));
```

— *end example*]

**3** Two values `i1` and `i2` of types `counted_iterator<I1>` and `counted_iterator<I2>` refer to elements of the same sequence if and only if `next(i1.base(), i1.count())` and `next(i2.base(), i2.count())` refer to the same (possibly past-the-end) element.

```cpp
namespace std {
  template<input_or_output_iterator I>
  class counted_iterator {
  public:
    using iterator_type = I;

    constexpr counted_iterator() = default;
    constexpr counted_iterator(I x, iter_difference_t<I> n);
    template<class I2>
      requires convertible_to<const I2&, I>
        constexpr counted_iterator(const counted_iterator<I2>& x);

    template<class I2>
      requires assignable_from<I&, const I2&>
        constexpr counted_iterator& operator=(const counted_iterator<I2>& x);

    constexpr I base() const & requires copy_constructible<I>;
    constexpr I base() &&;
    constexpr iter_difference_t<I> count() const noexcept;
    constexpr decltype(auto) operator*();
    constexpr decltype(auto) operator*() const
      requires dereferenceable<const I>;

    constexpr counted_iterator& operator++();
    decltype(auto) operator++(int);
    constexpr counted_iterator operator++(int)
      requires forward_iterator<I>;
    constexpr counted_iterator& operator--()
      requires bidirectional_iterator<I>;
    constexpr counted_iterator operator--(int)
      requires bidirectional_iterator<I>;

    constexpr counted_iterator operator+(iter_difference_t<I> n) const
      requires random_access_iterator<I>;
    friend constexpr counted_iterator operator+(
      iter_difference_t<I> n, const counted_iterator& x)
        requires random_access_iterator<I>;
    constexpr counted_iterator& operator+=(iter_difference_t<I> n)
      requires random_access_iterator<I>;

    constexpr counted_iterator operator-(iter_difference_t<I> n) const
      requires random_access_iterator<I>;
    template<common_with<I> I2>
      friend constexpr iter_difference_t<I2> operator-(
        const counted_iterator& x, const counted_iterator<I2>& y);
    friend constexpr iter_difference_t<I> operator-(
      const counted_iterator& x, default_sentinel_t);
    friend constexpr iter_difference_t<I> operator-(
      default_sentinel_t, const counted_iterator& y);
    constexpr counted_iterator& operator-=(iter_difference_t<I> n)
      requires random_access_iterator<I>;

    constexpr decltype(auto) operator[](iter_difference_t<I> n) const
      requires random_access_iterator<I>;

    template<common_with<I> I2>
      friend constexpr bool operator==(
        const counted_iterator& x, const counted_iterator<I2>& y);
    friend constexpr bool operator==(
      const counted_iterator& x, default_sentinel_t);

    template<common_with<I> I2>
      friend constexpr strong_ordering operator<=>(
        const counted_iterator& x, const counted_iterator<I2>& y);

    friend constexpr iter_rvalue_reference_t<I> iter_move(const counted_iterator& i)
      noexcept(noexcept(ranges::iter_move(i.current)))
        requires input_iterator<I>;
    template<indirectly_swappable<I> I2>
      friend constexpr void iter_swap(const counted_iterator& x, const counted_iterator<I2>& y)
        noexcept(noexcept(ranges::iter_swap(x.current, y.current)));

  private:
    I current = I();                    // exposition only
    iter_difference_t<I> length = 0;    // exposition only
  };

  template<class I>
  struct incrementable_traits<counted_iterator<I>> {
    using difference_type = iter_difference_t<I>;
  };

  template<input_iterator I>
  struct iterator_traits<counted_iterator<I>> : iterator_traits<I> {
    using pointer = void;
  };
}
```
