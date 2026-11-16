# 20.10.9.2 Static member functions [allocator.traits.members]

[Contents](../../README.md) · up: [20.10.9 Allocator traits \[allocator.traits\]](allocator.traits.md) · prev: [20.10.9.1 Member types \[allocator.traits.types\]](allocator.traits.types.md) · next: [20.10.10 The default allocator \[default.allocator\]](default.allocator.md)

```cpp
[[nodiscard]] static constexpr pointer allocate(Alloc& a, size_type n);
```

**1** *Returns:* `a.allocate(n)`.

```cpp
[[nodiscard]] static constexpr pointer allocate(Alloc& a, size_type n, const_void_pointer hint);
```

**2** *Returns:* `a.allocate(n, hint)` if that expression is well-formed; otherwise, `a.allocate(n)`.

```cpp
static constexpr void deallocate(Alloc& a, pointer p, size_type n);
```

**3** *Effects:* Calls `a.deallocate(p, n)`.

**4** *Throws:* Nothing.

```cpp
template<class T, class... Args>
  static constexpr void construct(Alloc& a, T* p, Args&&... args);
```

**5** *Effects:* Calls `a.construct(p, std::forward<Args>(args)...)` if that call is well-formed; otherwise, invokes `construct_at(p, std::forward<Args>(args)...)`.

```cpp
template<class T>
  static constexpr void destroy(Alloc& a, T* p);
```

**6** *Effects:* Calls `a.destroy(p)` if that call is well-formed; otherwise, invokes `destroy_at(p)`.

```cpp
static constexpr size_type max_size(const Alloc& a) noexcept;
```

**7** *Returns:* `a.max_size()` if that expression is well-formed; otherwise, `numeric_limits<size_type>::max()/sizeof(value_type)`.

```cpp
static constexpr Alloc select_on_container_copy_construction(const Alloc& rhs);
```

**8** *Returns:* `rhs.select_on_container_copy_construction()` if that expression is well-formed; otherwise, `rhs`.
