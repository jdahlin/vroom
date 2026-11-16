# 23.5.2 Insert iterators [insert.iterators]

[Contents](../../README.md) · up: [23.5 Iterator adaptors \[predef.iterators\]](predef.iterators.md) · prev: [23.5.1.8 Non-member functions \[reverse.iter.nonmember\]](reverse.iter.nonmember.md) · next: [23.5.2.1 Class template `back_insert_iterator` \[back.insert.iterator\]](back.insert.iterator.md)

**1** To make it possible to deal with insertion in the same way as writing into an array, a special kind of iterator adaptors, called *insert iterators*, are provided in the library. With regular iterator classes,

```cpp
while (first != last) *result++ = *first++;
```

causes a range `[first, last)` to be copied into a range starting with result. The same code with `result` being an insert iterator will insert corresponding elements into the container. This device allows all of the copying algorithms in the library to work in the *insert mode* instead of the *regular overwrite* mode.

**2** An insert iterator is constructed from a container and possibly one of its iterators pointing to where insertion takes place if it is neither at the beginning nor at the end of the container. Insert iterators meet the requirements of output iterators. `operator*` returns the insert iterator itself. The assignment `operator=(const T& x)` is defined on insert iterators to allow writing into them, it inserts `x` right before where the insert iterator is pointing. In other words, an insert iterator is like a cursor pointing into the container where the insertion takes place. `back_insert_iterator` inserts elements at the end of a container, `front_insert_iterator` inserts elements at the beginning of a container, and `insert_iterator` inserts elements where the iterator points to in a container. `back_inserter`, `front_inserter`, and `inserter` are three functions making the insert iterators out of a container.

## Subclauses

- [23.5.2.1 Class template `back_insert_iterator` \[back.insert.iterator\]](back.insert.iterator.md)
  - [23.5.2.1.1 Operations \[back.insert.iter.ops\]](back.insert.iter.ops.md)
  - [23.5.2.1.2 `back_inserter` \[back.inserter\]](back.inserter.md)
- [23.5.2.2 Class template `front_insert_iterator` \[front.insert.iterator\]](front.insert.iterator.md)
  - [23.5.2.2.1 Operations \[front.insert.iter.ops\]](front.insert.iter.ops.md)
  - [23.5.2.2.2 `front_inserter` \[front.inserter\]](front.inserter.md)
- [23.5.2.3 Class template `insert_iterator` \[insert.iterator\]](insert.iterator.md)
  - [23.5.2.3.1 Operations \[insert.iter.ops\]](insert.iter.ops.md)
  - [23.5.2.3.2 `inserter` \[inserter\]](inserter.md)
