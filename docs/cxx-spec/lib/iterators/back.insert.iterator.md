# 23.5.2.1 Class template `back_insert_iterator` [back.insert.iterator]

[Contents](../../README.md) · up: [23.5.2 Insert iterators \[insert.iterators\]](insert.iterators.md) · prev: [23.5.2 Insert iterators \[insert.iterators\]](insert.iterators.md) · next: [23.5.2.1.1 Operations \[back.insert.iter.ops\]](back.insert.iter.ops.md)

```cpp
namespace std {
  template<class Container>
  class back_insert_iterator {
  protected:
    Container* container = nullptr;

  public:
    using iterator_category = output_iterator_tag;
    using value_type        = void;
    using difference_type   = ptrdiff_t;
    using pointer           = void;
    using reference         = void;
    using container_type    = Container;

    constexpr back_insert_iterator() noexcept = default;
    constexpr explicit back_insert_iterator(Container& x);
    constexpr back_insert_iterator& operator=(const typename Container::value_type& value);
    constexpr back_insert_iterator& operator=(typename Container::value_type&& value);

    constexpr back_insert_iterator& operator*();
    constexpr back_insert_iterator& operator++();
    constexpr back_insert_iterator  operator++(int);
  };
}
```

## Subclauses

- [23.5.2.1.1 Operations \[back.insert.iter.ops\]](back.insert.iter.ops.md)
- [23.5.2.1.2 `back_inserter` \[back.inserter\]](back.inserter.md)
