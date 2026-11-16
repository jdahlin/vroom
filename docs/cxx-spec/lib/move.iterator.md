# 23.5.3.1 Class template `move_iterator` [move.iterator]

[Contents](../README.md) · up: [23.5.3 Move iterators and sentinels \[move.iterators\]](move.iterators.md) · prev: [23.5.3 Move iterators and sentinels \[move.iterators\]](move.iterators.md) · next: [23.5.3.2 Requirements \[move.iter.requirements\]](move.iter.requirements.md)

```cpp
namespace std {
  template<class Iterator>
  class move_iterator {
  public:
    using iterator_type     = Iterator;
    using iterator_concept  = input_iterator_tag;
    using iterator_category = see below;
    using value_type        = iter_value_t<Iterator>;
    using difference_type   = iter_difference_t<Iterator>;
    using pointer           = Iterator;
    using reference         = iter_rvalue_reference_t<Iterator>;

    constexpr move_iterator();
    constexpr explicit move_iterator(Iterator i);
    template<class U> constexpr move_iterator(const move_iterator<U>& u);
    template<class U> constexpr move_iterator& operator=(const move_iterator<U>& u);

    constexpr iterator_type base() const &;
    constexpr iterator_type base() &&;
    constexpr reference operator*() const;

    constexpr move_iterator& operator++();
    constexpr auto operator++(int);
    constexpr move_iterator& operator--();
    constexpr move_iterator operator--(int);

    constexpr move_iterator operator+(difference_type n) const;
    constexpr move_iterator& operator+=(difference_type n);
    constexpr move_iterator operator-(difference_type n) const;
    constexpr move_iterator& operator-=(difference_type n);
    constexpr reference operator[](difference_type n) const;

    template<sentinel_for<Iterator> S>
      friend constexpr bool
        operator==(const move_iterator& x, const move_sentinel<S>& y);
    template<sized_sentinel_for<Iterator> S>
      friend constexpr iter_difference_t<Iterator>
        operator-(const move_sentinel<S>& x, const move_iterator& y);
    template<sized_sentinel_for<Iterator> S>
      friend constexpr iter_difference_t<Iterator>
        operator-(const move_iterator& x, const move_sentinel<S>& y);
    friend constexpr iter_rvalue_reference_t<Iterator>
      iter_move(const move_iterator& i)
        noexcept(noexcept(ranges::iter_move(i.current)));
    template<indirectly_swappable<Iterator> Iterator2>
      friend constexpr void
        iter_swap(const move_iterator& x, const move_iterator<Iterator2>& y)
          noexcept(noexcept(ranges::iter_swap(x.current, y.current)));

  private:
    Iterator current;   // exposition only
  };
}
```

**1** The member *typedef-name* `iterator_category` denotes

- `random_access_iterator_tag` if the type `iterator_traits<Iterator>::iterator_category` models `derived_from<random_access_iterator_tag>`, and
- `iterator_traits<Iterator>::iterator_category` otherwise.
