# 23.5.2.2 Class template `front_insert_iterator` [front.insert.iterator]

[Contents](../../README.md) · up: [23.5.2 Insert iterators \[insert.iterators\]](insert.iterators.md) · prev: [23.5.2.1.2 `back_inserter` \[back.inserter\]](back.inserter.md) · next: [23.5.2.2.1 Operations \[front.insert.iter.ops\]](front.insert.iter.ops.md)

```cpp
namespace std {
  template<class Container>
  class front_insert_iterator {
  protected:
    Container* container = nullptr;

  public:
    using iterator_category = output_iterator_tag;
    using value_type        = void;
    using difference_type   = ptrdiff_t;
    using pointer           = void;
    using reference         = void;
    using container_type    = Container;

    constexpr front_insert_iterator() noexcept = default;
    constexpr explicit front_insert_iterator(Container& x);
    constexpr front_insert_iterator& operator=(const typename Container::value_type& value);
    constexpr front_insert_iterator& operator=(typename Container::value_type&& value);

    constexpr front_insert_iterator& operator*();
    constexpr front_insert_iterator& operator++();
    constexpr front_insert_iterator  operator++(int);
  };
}
```

## Subclauses

- [23.5.2.2.1 Operations \[front.insert.iter.ops\]](front.insert.iter.ops.md)
- [23.5.2.2.2 `front_inserter` \[front.inserter\]](front.inserter.md)
