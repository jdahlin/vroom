# 20.12.5.4 Members [mem.res.pool.mem]

[Contents](../README.md) · up: [20.12.5 Pool resource classes \[mem.res.pool\]](mem.res.pool.md) · prev: [20.12.5.3 Constructors and destructors \[mem.res.pool.ctor\]](mem.res.pool.ctor.md) · next: [20.12.6 Class `monotonic_buffer_resource` \[mem.res.monotonic.buffer\]](mem.res.monotonic.buffer.md)

```cpp
void release();
```

**1** *Effects:* Calls `upstream_resource()->deallocate()` as necessary to release all allocated memory. [*Note 1*: The memory is released back to `upstream_resource()` even if `deallocate` has not been called for some of the allocated blocks. — *end note*]

```cpp
memory_resource* upstream_resource() const;
```

**2** *Returns:* The value of the `upstream` argument provided to the constructor of this object.

```cpp
pool_options options() const;
```

**3** *Returns:* The options that control the pooling behavior of this resource. The values in the returned struct may differ from those supplied to the pool resource constructor in that values of zero will be replaced with implementation-defined defaults, and sizes may be rounded to unspecified granularity.

```cpp
void* do_allocate(size_t bytes, size_t alignment) override;
```

**4** *Returns:* A pointer to allocated storage ([[basic.stc.dynamic.allocation]](../basic.stc.dynamic.allocation.md)) with a size of at least `bytes`. The size and alignment of the allocated memory shall meet the requirements for a class derived from `memory_resource` ([[mem.res.class]](mem.res.class.md)).

**5** *Effects:* If the pool selected for a block of size `bytes` is unable to satisfy the memory request from its own internal data structures, it will call `upstream_resource()->allocate()` to obtain more memory. If `bytes` is larger than that which the largest pool can handle, then memory will be allocated using `upstream_resource()->allocate()`.

**6** *Throws:* Nothing unless `upstream_resource()->allocate()` throws.

```cpp
void do_deallocate(void* p, size_t bytes, size_t alignment) override;
```

**7** *Effects:* Returns the memory at `p` to the pool. It is unspecified if, or under what circumstances, this operation will result in a call to `upstream_resource()->deallocate()`.

**8** *Throws:* Nothing.

```cpp
bool do_is_equal(const memory_resource& other) const noexcept override;
```

**9** *Returns:* `this == &other`.
