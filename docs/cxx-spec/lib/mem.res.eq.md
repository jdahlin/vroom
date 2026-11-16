# 20.12.2.3 Equality [mem.res.eq]

[Contents](../README.md) · up: [20.12.2 Class `memory_resource` \[mem.res.class\]](mem.res.class.md) · prev: [20.12.2.2 Private virtual member functions \[mem.res.private\]](mem.res.private.md) · next: [20.12.3 Class template `polymorphic_allocator` \[mem.poly.allocator.class\]](mem.poly.allocator.class.md)

```cpp
bool operator==(const memory_resource& a, const memory_resource& b) noexcept;
```

**1** *Returns:* `&a == &b || a.is_equal(b)`.
