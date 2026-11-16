# 20.13.5 Operators [scoped.adaptor.operators]

[Contents](../../README.md) · up: [20.13 Class template `scoped_allocator_adaptor` \[allocator.adaptor\]](allocator.adaptor.md) · prev: [20.13.4 Members \[allocator.adaptor.members\]](allocator.adaptor.members.md) · next: [20.14 Function objects \[function.objects\]](function.objects.md)

```cpp
template<class OuterA1, class OuterA2, class... InnerAllocs>
  bool operator==(const scoped_allocator_adaptor<OuterA1, InnerAllocs...>& a,
                  const scoped_allocator_adaptor<OuterA2, InnerAllocs...>& b) noexcept;
```

**1** *Returns:* If `sizeof...(InnerAllocs)` is zero,

```cpp
a.outer_allocator() == b.outer_allocator()
```

otherwise

```cpp
a.outer_allocator() == b.outer_allocator() && a.inner_allocator() == b.inner_allocator()
```
