# 20.12.2.1 Public member functions [mem.res.public]

[Contents](../../README.md) · up: [20.12.2 Class `memory_resource` \[mem.res.class\]](mem.res.class.md) · prev: [20.12.2 Class `memory_resource` \[mem.res.class\]](mem.res.class.md) · next: [20.12.2.2 Private virtual member functions \[mem.res.private\]](mem.res.private.md)

```cpp
~memory_resource();
```

**1** *Effects:* Destroys this `memory_resource`.

```cpp
[[nodiscard]] void* allocate(size_t bytes, size_t alignment = max_align);
```

**2** *Effects:* Equivalent to: `return do_allocate(bytes, alignment);`

```cpp
void deallocate(void* p, size_t bytes, size_t alignment = max_align);
```

**3** *Effects:* Equivalent to `do_deallocate(p, bytes, alignment)`.

```cpp
bool is_equal(const memory_resource& other) const noexcept;
```

**4** *Effects:* Equivalent to: `return do_is_equal(other);`
