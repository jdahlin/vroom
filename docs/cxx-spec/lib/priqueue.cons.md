# 22.6.5.2 Constructors [priqueue.cons]

[Contents](../README.md) · up: [22.6.5 Class template `priority_queue` \[priority.queue\]](priority.queue.md) · prev: [22.6.5.1 Overview \[priqueue.overview\]](priqueue.overview.md) · next: [22.6.5.3 Constructors with allocators \[priqueue.cons.alloc\]](priqueue.cons.alloc.md)

```cpp
priority_queue(const Compare& x, const Container& y);
priority_queue(const Compare& x, Container&& y);
```

**1** *Preconditions:* `x` defines a strict weak ordering ([[alg.sorting]](alg.sorting.md)).

**2** *Effects:* Initializes `comp` with `x` and `c` with `y` (copy constructing or move constructing as appropriate); calls `make_heap(c.begin(), c.end(), comp)`.

```cpp
template<class InputIterator>
  priority_queue(InputIterator first, InputIterator last, const Compare& x, const Container& y);
template<class InputIterator>
  priority_queue(InputIterator first, InputIterator last, const Compare& x = Compare(),
                 Container&& y = Container());
```

**3** *Preconditions:* `x` defines a strict weak ordering ([[alg.sorting]](alg.sorting.md)).

**4** *Effects:* Initializes `comp` with `x` and `c` with `y` (copy constructing or move constructing as appropriate); calls `c.insert(c.end(), first, last)`; and finally calls `make_heap(c.begin(), c.end(), comp)`.
