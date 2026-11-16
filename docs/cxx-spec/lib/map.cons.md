# 22.4.4.2 Constructors, copy, and assignment [map.cons]

[Contents](../README.md) · up: [22.4.4 Class template `map` \[map\]](map.md) · prev: [22.4.4.1 Overview \[map.overview\]](map.overview.md) · next: [22.4.4.3 Element access \[map.access\]](map.access.md)

```cpp
explicit map(const Compare& comp, const Allocator& = Allocator());
```

**1** *Effects:* Constructs an empty `map` using the specified comparison object and allocator.

**2** *Complexity:* Constant.

```cpp
template<class InputIterator>
  map(InputIterator first, InputIterator last,
      const Compare& comp = Compare(), const Allocator& = Allocator());
```

**3** *Effects:* Constructs an empty `map` using the specified comparison object and allocator, and inserts elements from the range `[first, last)`.

**4** *Complexity:* Linear in *N* if the range `[first, last)` is already sorted using `comp` and otherwise *N* log *N*, where *N* is `last - first`.
