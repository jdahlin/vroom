# 23.5.1.1 Class template `reverse_iterator` [reverse.iterator]

[Contents](../../README.md) · up: [23.5.1 Reverse iterators \[reverse.iterators\]](reverse.iterators.md) · prev: [23.5.1 Reverse iterators \[reverse.iterators\]](reverse.iterators.md) · next: [23.5.1.2 Requirements \[reverse.iter.requirements\]](reverse.iter.requirements.md)

```cpp
namespace std {
  template<class Iterator>
  class reverse_iterator {
  public:
    using iterator_type     = Iterator;
    using iterator_concept  = see below;
    using iterator_category = see below;
    using value_type        = iter_value_t<Iterator>;
    using difference_type   = iter_difference_t<Iterator>;
    using pointer           = typename iterator_traits<Iterator>::pointer;
    using reference         = iter_reference_t<Iterator>;

    constexpr reverse_iterator();
    constexpr explicit reverse_iterator(Iterator x);
    template<class U> constexpr reverse_iterator(const reverse_iterator<U>& u);
    template<class U> constexpr reverse_iterator& operator=(const reverse_iterator<U>& u);

    constexpr Iterator base() const;
    constexpr reference operator*() const;
    constexpr pointer   operator->() const requires see below;

    constexpr reverse_iterator& operator++();
    constexpr reverse_iterator  operator++(int);
    constexpr reverse_iterator& operator--();
    constexpr reverse_iterator  operator--(int);

    constexpr reverse_iterator  operator+ (difference_type n) const;
    constexpr reverse_iterator& operator+=(difference_type n);
    constexpr reverse_iterator  operator- (difference_type n) const;
    constexpr reverse_iterator& operator-=(difference_type n);
    constexpr unspecified operator[](difference_type n) const;

    friend constexpr iter_rvalue_reference_t<Iterator>
      iter_move(const reverse_iterator& i) noexcept(see below);
    template<indirectly_swappable<Iterator> Iterator2>
      friend constexpr void
        iter_swap(const reverse_iterator& x,
                  const reverse_iterator<Iterator2>& y) noexcept(see below);

  protected:
    Iterator current;
  };
}
```

**1** The member *typedef-name* `iterator_concept` denotes

- `random_access_iterator_tag` if `Iterator` models `random_access_iterator`, and
- `bidirectional_iterator_tag` otherwise.

**2** The member *typedef-name* `iterator_category` denotes

- `random_access_iterator_tag` if the type `iterator_traits<Iterator>::iterator_category` models `derived_from<random_access_iterator_tag>`, and
- `iterator_traits<Iterator>::iterator_category` otherwise.
