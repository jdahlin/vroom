# 22.6.4.3 Constructors with allocators [queue.cons.alloc]

[Contents](../../README.md) · up: [22.6.4 Class template `queue` \[queue\]](queue.md) · prev: [22.6.4.2 Constructors \[queue.cons\]](queue.cons.md) · next: [22.6.4.4 Operators \[queue.ops\]](queue.ops.md)

**1** If `uses_allocator_v<container_type, Alloc>` is `false` the constructors in this subclause shall not participate in overload resolution.

```cpp
template<class Alloc> explicit queue(const Alloc& a);
```

**2** *Effects:* Initializes `c` with `a`.

```cpp
template<class Alloc> queue(const container_type& cont, const Alloc& a);
```

**3** *Effects:* Initializes `c` with `cont` as the first argument and `a` as the second argument.

```cpp
template<class Alloc> queue(container_type&& cont, const Alloc& a);
```

**4** *Effects:* Initializes `c` with `std::move(cont)` as the first argument and `a` as the second argument.

```cpp
template<class Alloc> queue(const queue& q, const Alloc& a);
```

**5** *Effects:* Initializes `c` with `q.c` as the first argument and `a` as the second argument.

```cpp
template<class Alloc> queue(queue&& q, const Alloc& a);
```

**6** *Effects:* Initializes `c` with `std::move(q.c)` as the first argument and `a` as the second argument.
