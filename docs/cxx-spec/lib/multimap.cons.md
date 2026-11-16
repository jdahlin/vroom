# 22.4.5.2 Constructors [multimap.cons]

[Contents](../README.md) · up: [22.4.5 Class template `multimap` \[multimap\]](multimap.md) · prev: [22.4.5.1 Overview \[multimap.overview\]](multimap.overview.md) · next: [22.4.5.3 Modifiers \[multimap.modifiers\]](multimap.modifiers.md)

```cpp
explicit multimap(const Compare& comp, const Allocator& = Allocator());
```

**1** *Effects:* Constructs an empty `multimap` using the specified comparison object and allocator.

**2** *Complexity:* Constant.

```cpp
template<class InputIterator>
  multimap(InputIterator first, InputIterator last,
           const Compare& comp = Compare(),
           const Allocator& = Allocator());
```

**3** *Effects:* Constructs an empty `multimap` using the specified comparison object and allocator, and inserts elements from the range `[first, last)`.

**4** *Complexity:* Linear in *N* if the range `[first, last)` is already sorted using `comp` and otherwise *N* log *N*, where *N* is `last - first`.
