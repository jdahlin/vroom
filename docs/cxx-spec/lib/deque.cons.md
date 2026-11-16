# 22.3.8.2 Constructors, copy, and assignment [deque.cons]

[Contents](../README.md) · up: [22.3.8 Class template `deque` \[deque\]](deque.md) · prev: [22.3.8.1 Overview \[deque.overview\]](deque.overview.md) · next: [22.3.8.3 Capacity \[deque.capacity\]](deque.capacity.md)

```cpp
explicit deque(const Allocator&);
```

**1** *Effects:* Constructs an empty `deque`, using the specified allocator.

**2** *Complexity:* Constant.

```cpp
explicit deque(size_type n, const Allocator& = Allocator());
```

**3** *Effects:* Constructs a `deque` with `n` default-inserted elements using the specified allocator.

**4** *Preconditions:* `T` is *Cpp17DefaultInsertable* into `*this`.

**5** *Complexity:* Linear in `n`.

```cpp
deque(size_type n, const T& value, const Allocator& = Allocator());
```

**6** *Effects:* Constructs a `deque` with `n` copies of `value`, using the specified allocator.

**7** *Preconditions:* `T` is *Cpp17CopyInsertable* into `*this`.

**8** *Complexity:* Linear in `n`.

```cpp
template<class InputIterator>
  deque(InputIterator first, InputIterator last, const Allocator& = Allocator());
```

**9** *Effects:* Constructs a `deque` equal to the range `[first, last)`, using the specified allocator.

**10** *Complexity:* Linear in `distance(first, last)`.
