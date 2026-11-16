# 23.4.2 Iterator operations [iterator.operations]

[Contents](../../README.md) · up: [23.4 Iterator primitives \[iterator.primitives\]](iterator.primitives.md) · prev: [23.4.1 Standard iterator tags \[std.iterator.tags\]](std.iterator.tags.md) · next: [23.4.3 Range iterator operations \[range.iter.ops\]](range.iter.ops.md)

**1** Since only random access iterators provide `+` and `-` operators, the library provides two function templates `advance` and `distance`. These function templates use `+` and `-` for random access iterators (and are, therefore, constant time for them); for input, forward and bidirectional iterators they use `++` to provide linear time implementations.

```cpp
template<class InputIterator, class Distance>
  constexpr void advance(InputIterator& i, Distance n);
```

**2** *Preconditions:* `n` is negative only for bidirectional iterators.

**3** *Effects:* Increments `i` by `n` if `n` is non-negative, and decrements `i` by `-n` otherwise.

```cpp
template<class InputIterator>
  constexpr typename iterator_traits<InputIterator>::difference_type
    distance(InputIterator first, InputIterator last);
```

**4** *Preconditions:* `last` is reachable from `first`, or `InputIterator` meets the *Cpp17RandomAccessIterator* requirements and `first` is reachable from `last`.

**5** *Effects:* If `InputIterator` meets the *Cpp17RandomAccessIterator* requirements, returns `(last - first)`; otherwise, returns the number of increments needed to get from `first` to `last`.

```cpp
template<class InputIterator>
  constexpr InputIterator next(InputIterator x,
    typename iterator_traits<InputIterator>::difference_type n = 1);
```

**6** *Effects:* Equivalent to: `advance(x, n); return x;`

```cpp
template<class BidirectionalIterator>
  constexpr BidirectionalIterator prev(BidirectionalIterator x,
    typename iterator_traits<BidirectionalIterator>::difference_type n = 1);
```

**7** *Effects:* Equivalent to: `advance(x, -n); return x;`
