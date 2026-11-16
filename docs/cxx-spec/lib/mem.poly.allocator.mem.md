# 20.12.3.2 Member functions [mem.poly.allocator.mem]

[Contents](../README.md) · up: [20.12.3 Class template `polymorphic_allocator` \[mem.poly.allocator.class\]](mem.poly.allocator.class.md) · prev: [20.12.3.1 Constructors \[mem.poly.allocator.ctor\]](mem.poly.allocator.ctor.md) · next: [20.12.3.3 Equality \[mem.poly.allocator.eq\]](mem.poly.allocator.eq.md)

```cpp
[[nodiscard]] Tp* allocate(size_t n);
```

**1** *Effects:* If `numeric_limits<size_t>::max() / sizeof(Tp) < n`, throws `bad_array_new_length`. Otherwise equivalent to:

```cpp
return static_cast<Tp*>(memory_rsrc->allocate(n * sizeof(Tp), alignof(Tp)));
```

```cpp
void deallocate(Tp* p, size_t n);
```

**2** *Preconditions:* `p` was allocated from a memory resource `x`, equal to `*memory_rsrc`, using `x.allocate(n * sizeof(Tp), alignof(Tp))`.

**3** *Effects:* Equivalent to `memory_rsrc->deallocate(p, n * sizeof(Tp), alignof(Tp))`.

**4** *Throws:* Nothing.

```cpp
[[nodiscard]] void* allocate_bytes(size_t nbytes, size_t alignment = alignof(max_align_t));
```

**5** *Effects:* Equivalent to: `return memory_rsrc->allocate(nbytes, alignment);`

**6** [*Note 1*: The return type is `void*` (rather than, e.g., `byte*`) to support conversion to an arbitrary pointer type `U*` by `static_cast<U*>`, thus facilitating construction of a `U` object in the allocated memory. — *end note*]

```cpp
void deallocate_bytes(void* p, size_t nbytes, size_t alignment = alignof(max_align_t));
```

**7** *Effects:* Equivalent to `memory_rsrc->deallocate(p, nbytes, alignment)`.

```cpp
template<class T>
  [[nodiscard]] T* allocate_object(size_t n = 1);
```

**8** *Effects:* Allocates memory suitable for holding an array of `n` objects of type `T`, as follows:

- if `numeric_limits<size_t>::max() / sizeof(T) < n`, throws `bad_array_new_length`,

- otherwise equivalent to:

  ```cpp
  return static_cast<T*>(allocate_bytes(n*sizeof(T), alignof(T)));
  ```

**9** [*Note 2*: `T` is not deduced and must therefore be provided as a template argument. — *end note*]

```cpp
template<class T>
  void deallocate_object(T* p, size_t n = 1);
```

**10** *Effects:* Equivalent to `deallocate_bytes(p, n*sizeof(T), alignof(T))`.

```cpp
template<class T, class CtorArgs...>
  [[nodiscard]] T* new_object(CtorArgs&&... ctor_args);
```

**11** *Effects:* Allocates and constructs an object of type `T`, as follows.<br>Equivalent to:

```cpp
T* p = allocate_object<T>();
try {
  construct(p, std::forward<CtorArgs>(ctor_args)...);
} catch (...) {
  deallocate_object(p);
  throw;
}
return p;
```

**12** [*Note 3*: `T` is not deduced and must therefore be provided as a template argument. — *end note*]

```cpp
template<class T>
  void delete_object(T* p);
```

**13** *Effects:* Equivalent to:

```cpp
destroy(p);
deallocate_object(p);
```

```cpp
template<class T, class... Args>
  void construct(T* p, Args&&... args);
```

**14** *Mandates:* Uses-allocator construction of `T` with allocator `*this` (see [[allocator.uses.construction]](allocator.uses.construction.md)) and constructor arguments `std::forward<Args>(args)...` is well-formed.

**15** *Effects:* Construct a `T` object in the storage whose address is represented by `p` by uses-allocator construction with allocator `*this` and constructor arguments `std::forward<Args>(args)...`.

**16** *Throws:* Nothing unless the constructor for `T` throws.

```cpp
template<class T>
  void destroy(T* p);
```

**17** *Effects:* As if by `p->~T()`.

```cpp
polymorphic_allocator select_on_container_copy_construction() const;
```

**18** *Returns:* `polymorphic_allocator()`.

**19** [*Note 4*: The memory resource is not propagated. — *end note*]

```cpp
memory_resource* resource() const;
```

**20** *Returns:* `memory_rsrc`.
