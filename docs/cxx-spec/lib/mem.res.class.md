# 20.12.2 Class `memory_resource` [mem.res.class]

[Contents](../README.md) · up: [20.12 Memory resources \[mem.res\]](mem.res.md) · prev: [20.12.1 Header `<memory_resource>` synopsis \[mem.res.syn\]](mem.res.syn.md) · next: [20.12.2.1 Public member functions \[mem.res.public\]](mem.res.public.md)

**1** The `memory_resource` class is an abstract interface to an unbounded set of classes encapsulating memory resources.

```cpp
namespace std::pmr {
  class memory_resource {
    static constexpr size_t max_align = alignof(max_align_t);   // exposition only

  public:
    memory_resource() = default;
    memory_resource(const memory_resource&) = default;
    virtual ~memory_resource();

    memory_resource& operator=(const memory_resource&) = default;

    [[nodiscard]] void* allocate(size_t bytes, size_t alignment = max_align);
    void deallocate(void* p, size_t bytes, size_t alignment = max_align);

    bool is_equal(const memory_resource& other) const noexcept;

  private:
    virtual void* do_allocate(size_t bytes, size_t alignment) = 0;
    virtual void do_deallocate(void* p, size_t bytes, size_t alignment) = 0;

    virtual bool do_is_equal(const memory_resource& other) const noexcept = 0;
  };
}
```

## Subclauses

- [20.12.2.1 Public member functions \[mem.res.public\]](mem.res.public.md)
- [20.12.2.2 Private virtual member functions \[mem.res.private\]](mem.res.private.md)
- [20.12.2.3 Equality \[mem.res.eq\]](mem.res.eq.md)
