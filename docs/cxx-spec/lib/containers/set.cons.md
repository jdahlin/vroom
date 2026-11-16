# 22.4.6.2 Constructors, copy, and assignment [set.cons]

[Contents](../../README.md) · up: [22.4.6 Class template `set` \[set\]](set.md) · prev: [22.4.6.1 Overview \[set.overview\]](set.overview.md) · next: [22.4.6.3 Erasure \[set.erasure\]](set.erasure.md)

```cpp
explicit set(const Compare& comp, const Allocator& = Allocator());
```

**1** *Effects:* Constructs an empty `set` using the specified comparison objects and allocator.

**2** *Complexity:* Constant.

```cpp
template<class InputIterator>
  set(InputIterator first, InputIterator last,
      const Compare& comp = Compare(), const Allocator& = Allocator());
```

**3** *Effects:* Constructs an empty `set` using the specified comparison object and allocator, and inserts elements from the range `[first, last)`.

**4** *Complexity:* Linear in *N* if the range `[first, last)` is already sorted using `comp` and otherwise *N* log *N*, where *N* is `last - first`.
