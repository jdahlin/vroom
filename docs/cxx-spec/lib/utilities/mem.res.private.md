# 20.12.2.2 Private virtual member functions [mem.res.private]

[Contents](../../README.md) · up: [20.12.2 Class `memory_resource` \[mem.res.class\]](mem.res.class.md) · prev: [20.12.2.1 Public member functions \[mem.res.public\]](mem.res.public.md) · next: [20.12.2.3 Equality \[mem.res.eq\]](mem.res.eq.md)

```cpp
virtual void* do_allocate(size_t bytes, size_t alignment) = 0;
```

**1** *Preconditions:* `alignment` is a power of two.

**2** *Returns:* A derived class shall implement this function to return a pointer to allocated storage ([[basic.stc.dynamic.allocation]](../../basic/basic.stc.dynamic.allocation.md)) with a size of at least `bytes`, aligned to the specified `alignment`.

**3** *Throws:* A derived class implementation shall throw an appropriate exception if it is unable to allocate memory with the requested size and alignment.

```cpp
virtual void do_deallocate(void* p, size_t bytes, size_t alignment) = 0;
```

**4** *Preconditions:* `p` was returned from a prior call to `allocate(bytes, alignment)` on a memory resource equal to `*this`, and the storage at `p` has not yet been deallocated.

**5** *Effects:* A derived class shall implement this function to dispose of allocated storage.

**6** *Throws:* Nothing.

```cpp
virtual bool do_is_equal(const memory_resource& other) const noexcept = 0;
```

**7** *Returns:* A derived class shall implement this function to return `true` if memory allocated from `this` can be deallocated from `other` and vice-versa, otherwise `false`. [*Note 1*: The most-derived type of `other` might not match the type of `this`. For a derived class `D`, an implementation of this function could immediately return `false` if `dynamic_cast<const D*>(&other) == nullptr`. — *end note*]
