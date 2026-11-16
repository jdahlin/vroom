# 22.3.10.2 Constructors, copy, and assignment [list.cons]

[Contents](../../README.md) · up: [22.3.10 Class template `list` \[list\]](list.md) · prev: [22.3.10.1 Overview \[list.overview\]](list.overview.md) · next: [22.3.10.3 Capacity \[list.capacity\]](list.capacity.md)

```cpp
explicit list(const Allocator&);
```

**1** *Effects:* Constructs an empty list, using the specified allocator.

**2** *Complexity:* Constant.

```cpp
explicit list(size_type n, const Allocator& = Allocator());
```

**3** *Preconditions:* `T` is *Cpp17DefaultInsertable* into `*this`.

**4** *Effects:* Constructs a `list` with `n` default-inserted elements using the specified allocator.

**5** *Complexity:* Linear in `n`.

```cpp
list(size_type n, const T& value, const Allocator& = Allocator());
```

**6** *Preconditions:* `T` is *Cpp17CopyInsertable* into `*this`.

**7** *Effects:* Constructs a `list` with `n` copies of `value`, using the specified allocator.

**8** *Complexity:* Linear in `n`.

```cpp
template<class InputIterator>
  list(InputIterator first, InputIterator last, const Allocator& = Allocator());
```

**9** *Effects:* Constructs a `list` equal to the range `[first, last)`.

**10** *Complexity:* Linear in `distance(first, last)`.
