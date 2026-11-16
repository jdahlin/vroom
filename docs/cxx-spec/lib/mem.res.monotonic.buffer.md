# 20.12.6 Class `monotonic_buffer_resource` [mem.res.monotonic.buffer]

[Contents](../README.md) · up: [20.12 Memory resources \[mem.res\]](mem.res.md) · prev: [20.12.5.4 Members \[mem.res.pool.mem\]](mem.res.pool.mem.md) · next: [20.12.6.1 Constructors and destructor \[mem.res.monotonic.buffer.ctor\]](mem.res.monotonic.buffer.ctor.md)

**1** A `monotonic_buffer_resource` is a special-purpose memory resource intended for very fast memory allocations in situations where memory is used to build up a few objects and then is released all at once when the memory resource object is destroyed. It has the following qualities:

- A call to `deallocate` has no effect, thus the amount of memory consumed increases monotonically until the resource is destroyed.
- The program can supply an initial buffer, which the allocator uses to satisfy memory requests.
- When the initial buffer (if any) is exhausted, it obtains additional buffers from an *upstream* memory resource supplied at construction. Each additional buffer is larger than the previous one, following a geometric progression.
- It is intended for access from one thread of control at a time. Specifically, calls to `allocate` and `deallocate` do not synchronize with one another.
- It frees the allocated memory on destruction, even if `deallocate` has not been called for some of the allocated blocks.

```cpp
namespace std::pmr {
  class monotonic_buffer_resource : public memory_resource {
    memory_resource* upstream_rsrc;     // exposition only
    void* current_buffer;               // exposition only
    size_t next_buffer_size;            // exposition only

  public:
    explicit monotonic_buffer_resource(memory_resource* upstream);
    monotonic_buffer_resource(size_t initial_size, memory_resource* upstream);
    monotonic_buffer_resource(void* buffer, size_t buffer_size, memory_resource* upstream);

    monotonic_buffer_resource()
      : monotonic_buffer_resource(get_default_resource()) {}
    explicit monotonic_buffer_resource(size_t initial_size)
      : monotonic_buffer_resource(initial_size, get_default_resource()) {}
    monotonic_buffer_resource(void* buffer, size_t buffer_size)
      : monotonic_buffer_resource(buffer, buffer_size, get_default_resource()) {}

    monotonic_buffer_resource(const monotonic_buffer_resource&) = delete;

    virtual ~monotonic_buffer_resource();

    monotonic_buffer_resource& operator=(const monotonic_buffer_resource&) = delete;

    void release();
    memory_resource* upstream_resource() const;

  protected:
    void* do_allocate(size_t bytes, size_t alignment) override;
    void do_deallocate(void* p, size_t bytes, size_t alignment) override;

    bool do_is_equal(const memory_resource& other) const noexcept override;
  };
}
```

## Subclauses

- [20.12.6.1 Constructors and destructor \[mem.res.monotonic.buffer.ctor\]](mem.res.monotonic.buffer.ctor.md)
- [20.12.6.2 Members \[mem.res.monotonic.buffer.mem\]](mem.res.monotonic.buffer.mem.md)
