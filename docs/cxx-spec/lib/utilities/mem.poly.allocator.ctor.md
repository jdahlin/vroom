# 20.12.3.1 Constructors [mem.poly.allocator.ctor]

[Contents](../../README.md) · up: [20.12.3 Class template `polymorphic_allocator` \[mem.poly.allocator.class\]](mem.poly.allocator.class.md) · prev: [20.12.3 Class template `polymorphic_allocator` \[mem.poly.allocator.class\]](mem.poly.allocator.class.md) · next: [20.12.3.2 Member functions \[mem.poly.allocator.mem\]](mem.poly.allocator.mem.md)

```cpp
polymorphic_allocator() noexcept;
```

**1** *Effects:* Sets `memory_rsrc` to `get_default_resource()`.

```cpp
polymorphic_allocator(memory_resource* r);
```

**2** *Preconditions:* `r` is non-null.

**3** *Effects:* Sets `memory_rsrc` to `r`.

**4** *Throws:* Nothing.

**5** [*Note 1*: This constructor provides an implicit conversion from `memory_resource*`. — *end note*]

```cpp
template<class U> polymorphic_allocator(const polymorphic_allocator<U>& other) noexcept;
```

**6** *Effects:* Sets `memory_rsrc` to `other.resource()`.
