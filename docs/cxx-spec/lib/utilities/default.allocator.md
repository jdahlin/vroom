# 20.10.10 The default allocator [default.allocator]

[Contents](../../README.md) · up: [20.10 Memory \[memory\]](memory.md) · prev: [20.10.9.2 Static member functions \[allocator.traits.members\]](allocator.traits.members.md) · next: [20.10.10.1 Members \[allocator.members\]](allocator.members.md)

**1** All specializations of the default allocator meet the allocator completeness requirements ([[allocator.requirements.completeness]](../library/allocator.requirements.completeness.md)).

```cpp
namespace std {
  template<class T> class allocator {
   public:
    using value_type                             = T;
    using size_type                              = size_t;
    using difference_type                        = ptrdiff_t;
    using propagate_on_container_move_assignment = true_type;
    using is_always_equal                        = true_type;

    constexpr allocator() noexcept;
    constexpr allocator(const allocator&) noexcept;
    template<class U> constexpr allocator(const allocator<U>&) noexcept;
    constexpr ~allocator();
    constexpr allocator& operator=(const allocator&) = default;

    [[nodiscard]] constexpr T* allocate(size_t n);
    constexpr void deallocate(T* p, size_t n);
  };
}
```

## Subclauses

- [20.10.10.1 Members \[allocator.members\]](allocator.members.md)
- [20.10.10.2 Operators \[allocator.globals\]](allocator.globals.md)
