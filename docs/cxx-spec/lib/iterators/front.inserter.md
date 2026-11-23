# 23.5.2.2.2 `front_inserter` [front.inserter]

[Contents](../../README.md) · up: [23.5.2.2 Class template `front_insert_iterator` \[front.insert.iterator\]](front.insert.iterator.md) · prev: [23.5.2.2.1 Operations \[front.insert.iter.ops\]](front.insert.iter.ops.md) · next: [23.5.2.3 Class template `insert_iterator` \[insert.iterator\]](insert.iterator.md)

```cpp
template<class Container>
  constexpr front_insert_iterator<Container> front_inserter(Container& x);
```

**1** *Returns:* `front_insert_iterator<Container>(x)`.
