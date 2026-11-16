# 22.6.5.3 Constructors with allocators [priqueue.cons.alloc]

[Contents](../../README.md) · up: [22.6.5 Class template `priority_queue` \[priority.queue\]](priority.queue.md) · prev: [22.6.5.2 Constructors \[priqueue.cons\]](priqueue.cons.md) · next: [22.6.5.4 Members \[priqueue.members\]](priqueue.members.md)

**1** If `uses_allocator_v<container_type, Alloc>` is `false` the constructors in this subclause shall not participate in overload resolution.

```cpp
template<class Alloc> explicit priority_queue(const Alloc& a);
```

**2** *Effects:* Initializes `c` with `a` and value-initializes `comp`.

```cpp
template<class Alloc> priority_queue(const Compare& compare, const Alloc& a);
```

**3** *Effects:* Initializes `c` with `a` and initializes `comp` with `compare`.

```cpp
template<class Alloc>
  priority_queue(const Compare& compare, const Container& cont, const Alloc& a);
```

**4** *Effects:* Initializes `c` with `cont` as the first argument and `a` as the second argument, and initializes `comp` with `compare`; calls `make_heap(c.begin(), c.end(), comp)`.

```cpp
template<class Alloc>
  priority_queue(const Compare& compare, Container&& cont, const Alloc& a);
```

**5** *Effects:* Initializes `c` with `std::move(cont)` as the first argument and `a` as the second argument, and initializes `comp` with `compare`; calls `make_heap(c.begin(), c.end(), comp)`.

```cpp
template<class Alloc> priority_queue(const priority_queue& q, const Alloc& a);
```

**6** *Effects:* Initializes `c` with `q.c` as the first argument and `a` as the second argument, and initializes `comp` with `q.comp`.

```cpp
template<class Alloc> priority_queue(priority_queue&& q, const Alloc& a);
```

**7** *Effects:* Initializes `c` with `std::move(q.c)` as the first argument and `a` as the second argument, and initializes `comp` with `std::move(q.comp)`.
