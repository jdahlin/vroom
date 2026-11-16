# 23.5.2.3.2 `inserter` [inserter]

[Contents](../../README.md) · up: [23.5.2.3 Class template `insert_iterator` \[insert.iterator\]](insert.iterator.md) · prev: [23.5.2.3.1 Operations \[insert.iter.ops\]](insert.iter.ops.md) · next: [23.5.3 Move iterators and sentinels \[move.iterators\]](move.iterators.md)

```cpp
template<class Container>
  constexpr insert_iterator<Container>
    inserter(Container& x, ranges::iterator_t<Container> i);
```

**1** *Returns:* `insert_iterator<Container>(x, i)`.
