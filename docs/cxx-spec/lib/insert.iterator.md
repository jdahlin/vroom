# 23.5.2.3 Class template `insert_iterator` [insert.iterator]

[Contents](../README.md) · up: [23.5.2 Insert iterators \[insert.iterators\]](insert.iterators.md) · prev: [23.5.2.2.2 `front_inserter` \[front.inserter\]](front.inserter.md) · next: [23.5.2.3.1 Operations \[insert.iter.ops\]](insert.iter.ops.md)

```cpp
namespace std {
  template<class Container>
  class insert_iterator {
  protected:
    Container* container = nullptr;
    ranges::iterator_t<Container> iter = ranges::iterator_t<Container>();

  public:
    using iterator_category = output_iterator_tag;
    using value_type        = void;
    using difference_type   = ptrdiff_t;
    using pointer           = void;
    using reference         = void;
    using container_type    = Container;

    insert_iterator() = default;
    constexpr insert_iterator(Container& x, ranges::iterator_t<Container> i);
    constexpr insert_iterator& operator=(const typename Container::value_type& value);
    constexpr insert_iterator& operator=(typename Container::value_type&& value);

    constexpr insert_iterator& operator*();
    constexpr insert_iterator& operator++();
    constexpr insert_iterator& operator++(int);
  };
}
```

## Subclauses

- [23.5.2.3.1 Operations \[insert.iter.ops\]](insert.iter.ops.md)
- [23.5.2.3.2 `inserter` \[inserter\]](inserter.md)
