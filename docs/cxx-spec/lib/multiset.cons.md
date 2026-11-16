# 22.4.7.2 Constructors [multiset.cons]

[Contents](../README.md) · up: [22.4.7 Class template `multiset` \[multiset\]](multiset.md) · prev: [22.4.7.1 Overview \[multiset.overview\]](multiset.overview.md) · next: [22.4.7.3 Erasure \[multiset.erasure\]](multiset.erasure.md)

```cpp
explicit multiset(const Compare& comp, const Allocator& = Allocator());
```

**1** *Effects:* Constructs an empty `multiset` using the specified comparison object and allocator.

**2** *Complexity:* Constant.

```cpp
template<class InputIterator>
  multiset(InputIterator first, InputIterator last,
           const Compare& comp = Compare(), const Allocator& = Allocator());
```

**3** *Effects:* Constructs an empty `multiset` using the specified comparison object and allocator, and inserts elements from the range `[first, last)`.

**4** *Complexity:* Linear in *N* if the range `[first, last)` is already sorted using `comp` and otherwise *N* log *N*, where *N* is `last - first`.
