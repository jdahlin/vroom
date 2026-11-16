# 20.12.4 Access to program-wide `memory_resource` objects [mem.res.global]

[Contents](../README.md) · up: [20.12 Memory resources \[mem.res\]](mem.res.md) · prev: [20.12.3.3 Equality \[mem.poly.allocator.eq\]](mem.poly.allocator.eq.md) · next: [20.12.5 Pool resource classes \[mem.res.pool\]](mem.res.pool.md)

```cpp
memory_resource* new_delete_resource() noexcept;
```

**1** *Returns:* A pointer to a static-duration object of a type derived from `memory_resource` that can serve as a resource for allocating memory using `::operator new` and `::operator delete`. The same value is returned every time this function is called. For a return value `p` and a memory resource `r`, `p->is_equal(r)` returns `&r == p`.

```cpp
memory_resource* null_memory_resource() noexcept;
```

**2** *Returns:* A pointer to a static-duration object of a type derived from `memory_resource` for which `allocate()` always throws `bad_alloc` and for which `deallocate()` has no effect. The same value is returned every time this function is called. For a return value `p` and a memory resource `r`, `p->is_equal(r)` returns `&r == p`.

**3** The *default memory resource pointer* is a pointer to a memory resource that is used by certain facilities when an explicit memory resource is not supplied through the interface. Its initial value is the return value of `new_delete_resource()`.

```cpp
memory_resource* set_default_resource(memory_resource* r) noexcept;
```

**4** *Effects:* If `r` is non-null, sets the value of the default memory resource pointer to `r`, otherwise sets the default memory resource pointer to `new_delete_resource()`.

**5** *Returns:* The previous value of the default memory resource pointer.

**6** *Remarks:* Calling the `set_default_resource` and `get_default_resource` functions shall not incur a data race. A call to the `set_default_resource` function shall synchronize with subsequent calls to the `set_default_resource` and `get_default_resource` functions.

```cpp
memory_resource* get_default_resource() noexcept;
```

**7** *Returns:* The current value of the default memory resource pointer.
