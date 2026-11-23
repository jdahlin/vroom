# 20.12.3.3 Equality [mem.poly.allocator.eq]

[Contents](../../README.md) · up: [20.12.3 Class template `polymorphic_allocator` \[mem.poly.allocator.class\]](mem.poly.allocator.class.md) · prev: [20.12.3.2 Member functions \[mem.poly.allocator.mem\]](mem.poly.allocator.mem.md) · next: [20.12.4 Access to program-wide `memory_resource` objects \[mem.res.global\]](mem.res.global.md)

```cpp
template<class T1, class T2>
  bool operator==(const polymorphic_allocator<T1>& a,
                  const polymorphic_allocator<T2>& b) noexcept;
```

**1** *Returns:* `*a.resource() == *b.resource()`.
