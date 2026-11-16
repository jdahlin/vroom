# 20.13.4 Members [allocator.adaptor.members]

[Contents](../../README.md) · up: [20.13 Class template `scoped_allocator_adaptor` \[allocator.adaptor\]](allocator.adaptor.md) · prev: [20.13.3 Constructors \[allocator.adaptor.cnstr\]](allocator.adaptor.cnstr.md) · next: [20.13.5 Operators \[scoped.adaptor.operators\]](scoped.adaptor.operators.md)

**1** In the `construct` member functions, `OUTERMOST(x)` is `OUTERMOST(x.outer_allocator())` if the expression `x.outer_allocator()` is valid ([[temp.deduct]](../../temp/temp.deduct.md)) and `x` otherwise; `OUTERMOST_ALLOC_TRAITS(x)` is `allocator_traits<remove_reference_t<decltype(OUTERMOST(x))>>`. [*Note 1*: `OUTERMOST(x)` and `OUTERMOST_ALLOC_TRAITS(x)` are recursive operations. It is incumbent upon the definition of `outer_allocator()` to ensure that the recursion terminates. It will terminate for all instantiations of `scoped_allocator_adaptor`. — *end note*]

```cpp
inner_allocator_type& inner_allocator() noexcept;
const inner_allocator_type& inner_allocator() const noexcept;
```

**2** *Returns:* `*this` if `sizeof...(InnerAllocs)` is zero; otherwise, `inner`.

```cpp
outer_allocator_type& outer_allocator() noexcept;
```

**3** *Returns:* `static_cast<OuterAlloc&>(*this)`.

```cpp
const outer_allocator_type& outer_allocator() const noexcept;
```

**4** *Returns:* `static_cast<const OuterAlloc&>(*this)`.

```cpp
[[nodiscard]] pointer allocate(size_type n);
```

**5** *Returns:* `allocator_traits<OuterAlloc>::allocate(outer_allocator(), n)`.

```cpp
[[nodiscard]] pointer allocate(size_type n, const_void_pointer hint);
```

**6** *Returns:* `allocator_traits<OuterAlloc>::allocate(outer_allocator(), n, hint)`.

```cpp
void deallocate(pointer p, size_type n) noexcept;
```

**7** *Effects:* As if by: `allocator_traits<OuterAlloc>::deallocate(outer_allocator(), p, n);`

```cpp
size_type max_size() const;
```

**8** *Returns:* `allocator_traits<OuterAlloc>::max_size(outer_allocator())`.

```cpp
template<class T, class... Args>
  void construct(T* p, Args&&... args);
```

**9** *Effects:* Equivalent to:

```cpp
apply([p, this](auto&&... newargs) {
        OUTERMOST_ALLOC_TRAITS(*this)::construct(
          OUTERMOST(*this), p,
          std::forward<decltype(newargs)>(newargs)...);
      },
      uses_allocator_construction_args<T>(inner_allocator(),
                                          std::forward<Args>(args)...));
```

```cpp
template<class T>
  void destroy(T* p);
```

**10** *Effects:* Calls `OUTERMOST_ALLOC_TRAITS(*this)::destroy(OUTERMOST(*this), p)`.

```cpp
scoped_allocator_adaptor select_on_container_copy_construction() const;
```

**11** *Returns:* A new `scoped_allocator_adaptor` object where each allocator `A` in the adaptor is initialized from the result of calling `allocator_traits<A>::select_on_container_copy_construction()` on the corresponding allocator in `*this`.
