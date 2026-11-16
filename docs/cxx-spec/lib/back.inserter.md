# 23.5.2.1.2 `back_inserter` [back.inserter]

[Contents](../README.md) · up: [23.5.2.1 Class template `back_insert_iterator` \[back.insert.iterator\]](back.insert.iterator.md) · prev: [23.5.2.1.1 Operations \[back.insert.iter.ops\]](back.insert.iter.ops.md) · next: [23.5.2.2 Class template `front_insert_iterator` \[front.insert.iterator\]](front.insert.iterator.md)

```cpp
template<class Container>
  constexpr back_insert_iterator<Container> back_inserter(Container& x);
```

**1** *Returns:* `back_insert_iterator<Container>(x)`.
