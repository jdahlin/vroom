# 22.3.9.2 Constructors, copy, and assignment [forwardlist.cons]

[Contents](../../README.md) · up: [22.3.9 Class template `forward_list` \[forwardlist\]](forwardlist.md) · prev: [22.3.9.1 Overview \[forwardlist.overview\]](forwardlist.overview.md) · next: [22.3.9.3 Iterators \[forwardlist.iter\]](forwardlist.iter.md)

```cpp
explicit forward_list(const Allocator&);
```

**1** *Effects:* Constructs an empty `forward_list` object using the specified allocator.

**2** *Complexity:* Constant.

```cpp
explicit forward_list(size_type n, const Allocator& = Allocator());
```

**3** *Preconditions:* `T` is *Cpp17DefaultInsertable* into `*this`.

**4** *Effects:* Constructs a `forward_list` object with `n` default-inserted elements using the specified allocator.

**5** *Complexity:* Linear in `n`.

```cpp
forward_list(size_type n, const T& value, const Allocator& = Allocator());
```

**6** *Preconditions:* `T` is *Cpp17CopyInsertable* into `*this`.

**7** *Effects:* Constructs a `forward_list` object with `n` copies of `value` using the specified allocator.

**8** *Complexity:* Linear in `n`.

```cpp
template<class InputIterator>
  forward_list(InputIterator first, InputIterator last, const Allocator& = Allocator());
```

**9** *Effects:* Constructs a `forward_list` object equal to the range `[first, last)`.

**10** *Complexity:* Linear in `distance(first, last)`.
