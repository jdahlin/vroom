# 20.12.3 Class template `polymorphic_allocator` [mem.poly.allocator.class]

[Contents](../README.md) · up: [20.12 Memory resources \[mem.res\]](mem.res.md) · prev: [20.12.2.3 Equality \[mem.res.eq\]](mem.res.eq.md) · next: [20.12.3.1 Constructors \[mem.poly.allocator.ctor\]](mem.poly.allocator.ctor.md)

**1** A specialization of class template `pmr::polymorphic_allocator` meets the *Cpp17Allocator* requirements ([Table 36](allocator.requirements.md#tab:cpp17.allocator)). Constructed with different memory resources, different instances of the same specialization of `pmr::polymorphic_allocator` can exhibit entirely different allocation behavior. This runtime polymorphism allows objects that use `polymorphic_allocator` to behave as if they used different allocator types at run time even though they use the same static allocator type.

**2** All specializations of class template `pmr::polymorphic_allocator` meet the allocator completeness requirements ([[allocator.requirements.completeness]](allocator.requirements.completeness.md)).

```cpp
namespace std::pmr {
  template<class Tp = byte> class polymorphic_allocator {
    memory_resource* memory_rsrc;       // exposition only

  public:
    using value_type = Tp;

    // [mem.poly.allocator.ctor], constructors
    polymorphic_allocator() noexcept;
    polymorphic_allocator(memory_resource* r);

    polymorphic_allocator(const polymorphic_allocator& other) = default;

    template<class U>
      polymorphic_allocator(const polymorphic_allocator<U>& other) noexcept;

    polymorphic_allocator& operator=(const polymorphic_allocator&) = delete;

    // [mem.poly.allocator.mem], member functions
    [[nodiscard]] Tp* allocate(size_t n);
    void deallocate(Tp* p, size_t n);

    [[nodiscard]] void* allocate_bytes(size_t nbytes, size_t alignment = alignof(max_align_t));
    void deallocate_bytes(void* p, size_t nbytes, size_t alignment = alignof(max_align_t));
    template<class T> [[nodiscard]] T* allocate_object(size_t n = 1);
    template<class T> void deallocate_object(T* p, size_t n = 1);
    template<class T, class... CtorArgs> [[nodiscard]] T* new_object(CtorArgs&&... ctor_args);
    template<class T> void delete_object(T* p);

    template<class T, class... Args>
      void construct(T* p, Args&&... args);

    template<class T>
      void destroy(T* p);

    polymorphic_allocator select_on_container_copy_construction() const;

    memory_resource* resource() const;
  };
}
```

## Subclauses

- [20.12.3.1 Constructors \[mem.poly.allocator.ctor\]](mem.poly.allocator.ctor.md)
- [20.12.3.2 Member functions \[mem.poly.allocator.mem\]](mem.poly.allocator.mem.md)
- [20.12.3.3 Equality \[mem.poly.allocator.eq\]](mem.poly.allocator.eq.md)
