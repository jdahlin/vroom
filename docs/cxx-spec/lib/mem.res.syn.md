# 20.12.1 Header `<memory_resource>` synopsis [mem.res.syn]

[Contents](../README.md) · up: [20.12 Memory resources \[mem.res\]](mem.res.md) · prev: [20.12 Memory resources \[mem.res\]](mem.res.md) · next: [20.12.2 Class `memory_resource` \[mem.res.class\]](mem.res.class.md)

```cpp
namespace std::pmr {
  // [mem.res.class], class memory_resource
  class memory_resource;

  bool operator==(const memory_resource& a, const memory_resource& b) noexcept;

  // [mem.poly.allocator.class], class template polymorphic_allocator
  template<class Tp> class polymorphic_allocator;

  template<class T1, class T2>
    bool operator==(const polymorphic_allocator<T1>& a,
                    const polymorphic_allocator<T2>& b) noexcept;

  // [mem.res.global], global memory resources
  memory_resource* new_delete_resource() noexcept;
  memory_resource* null_memory_resource() noexcept;
  memory_resource* set_default_resource(memory_resource* r) noexcept;
  memory_resource* get_default_resource() noexcept;

  // [mem.res.pool], pool resource classes
  struct pool_options;
  class synchronized_pool_resource;
  class unsynchronized_pool_resource;
  class monotonic_buffer_resource;
}
```
