# 20.12.6.2 Members [mem.res.monotonic.buffer.mem]

[Contents](../README.md) · up: [20.12.6 Class `monotonic_buffer_resource` \[mem.res.monotonic.buffer\]](mem.res.monotonic.buffer.md) · prev: [20.12.6.1 Constructors and destructor \[mem.res.monotonic.buffer.ctor\]](mem.res.monotonic.buffer.ctor.md) · next: [20.13 Class template `scoped_allocator_adaptor` \[allocator.adaptor\]](allocator.adaptor.md)

```cpp
void release();
```

**1** *Effects:* Calls `upstream_rsrc->deallocate()` as necessary to release all allocated memory.

**2** [*Note 1*: The memory is released back to `upstream_rsrc` even if some blocks that were allocated from `this` have not been deallocated from `this`. — *end note*]

```cpp
memory_resource* upstream_resource() const;
```

**3** *Returns:* The value of `upstream_rsrc`.

```cpp
void* do_allocate(size_t bytes, size_t alignment) override;
```

**4** *Returns:* A pointer to allocated storage ([[basic.stc.dynamic.allocation]](../basic.stc.dynamic.allocation.md)) with a size of at least `bytes`. The size and alignment of the allocated memory shall meet the requirements for a class derived from `memory_resource` ([[mem.res.class]](mem.res.class.md)).

**5** *Effects:* If the unused space in `current_buffer` can fit a block with the specified `bytes` and `alignment`, then allocate the return block from `current_buffer`; otherwise set `current_buffer` to `upstream_rsrc->allocate(n, m)`, where `n` is not less than `max(bytes, next_buffer_size)` and `m` is not less than `alignment`, and increase `next_buffer_size` by an implementation-defined growth factor (which need not be integral), then allocate the return block from the newly-allocated `current_buffer`.

**6** *Throws:* Nothing unless `upstream_rsrc->allocate()` throws.

```cpp
void do_deallocate(void* p, size_t bytes, size_t alignment) override;
```

**7** *Effects:* None.

**8** *Throws:* Nothing.

**9** *Remarks:* Memory used by this resource increases monotonically until its destruction.

```cpp
bool do_is_equal(const memory_resource& other) const noexcept override;
```

**10** *Returns:* `this == &other`.
