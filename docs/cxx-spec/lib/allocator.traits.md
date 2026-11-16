# 20.10.9 Allocator traits [allocator.traits]

[Contents](../README.md) · up: [20.10 Memory \[memory\]](memory.md) · prev: [20.10.8.2 Uses-allocator construction \[allocator.uses.construction\]](allocator.uses.construction.md) · next: [20.10.9.1 Member types \[allocator.traits.types\]](allocator.traits.types.md)

**1** The class template `allocator_traits` supplies a uniform interface to all allocator types. An allocator cannot be a non-class type, however, even if `allocator_traits` supplies the entire required interface. [*Note 1*: Thus, it is always possible to create a derived class from an allocator. — *end note*]

```cpp
namespace std {
  template<class Alloc> struct allocator_traits {
    using allocator_type     = Alloc;

    using value_type         = typename Alloc::value_type;

    using pointer            = see below;
    using const_pointer      = see below;
    using void_pointer       = see below;
    using const_void_pointer = see below;

    using difference_type    = see below;
    using size_type          = see below;

    using propagate_on_container_copy_assignment = see below;
    using propagate_on_container_move_assignment = see below;
    using propagate_on_container_swap            = see below;
    using is_always_equal                        = see below;

    template<class T> using rebind_alloc = see below;
    template<class T> using rebind_traits = allocator_traits<rebind_alloc<T>>;

    [[nodiscard]] static constexpr pointer allocate(Alloc& a, size_type n);
    [[nodiscard]] static constexpr pointer allocate(Alloc& a, size_type n,
                                                    const_void_pointer hint);

    static constexpr void deallocate(Alloc& a, pointer p, size_type n);

    template<class T, class... Args>
      static constexpr void construct(Alloc& a, T* p, Args&&... args);

    template<class T>
      static constexpr void destroy(Alloc& a, T* p);

    static constexpr size_type max_size(const Alloc& a) noexcept;

    static constexpr Alloc select_on_container_copy_construction(const Alloc& rhs);
  };
}
```

## Subclauses

- [20.10.9.1 Member types \[allocator.traits.types\]](allocator.traits.types.md)
- [20.10.9.2 Static member functions \[allocator.traits.members\]](allocator.traits.members.md)
