# 22.6.6.3 Constructors with allocators [stack.cons.alloc]

[Contents](../README.md) · up: [22.6.6 Class template `stack` \[stack\]](stack.md) · prev: [22.6.6.2 Constructors \[stack.cons\]](stack.cons.md) · next: [22.6.6.4 Operators \[stack.ops\]](stack.ops.md)

**1** If `uses_allocator_v<container_type, Alloc>` is `false` the constructors in this subclause shall not participate in overload resolution.

```cpp
template<class Alloc> explicit stack(const Alloc& a);
```

**2** *Effects:* Initializes `c` with `a`.

```cpp
template<class Alloc> stack(const container_type& cont, const Alloc& a);
```

**3** *Effects:* Initializes `c` with `cont` as the first argument and `a` as the second argument.

```cpp
template<class Alloc> stack(container_type&& cont, const Alloc& a);
```

**4** *Effects:* Initializes `c` with `std::move(cont)` as the first argument and `a` as the second argument.

```cpp
template<class Alloc> stack(const stack& s, const Alloc& a);
```

**5** *Effects:* Initializes `c` with `s.c` as the first argument and `a` as the second argument.

```cpp
template<class Alloc> stack(stack&& s, const Alloc& a);
```

**6** *Effects:* Initializes `c` with `std::move(s.c)` as the first argument and `a` as the second argument.
