# 22.3.11.2 Constructors, copy, and assignment [vector.cons]

[Contents](../../README.md) · up: [22.3.11 Class template `vector` \[vector\]](vector.md) · prev: [22.3.11.1 Overview \[vector.overview\]](vector.overview.md) · next: [22.3.11.3 Capacity \[vector.capacity\]](vector.capacity.md)

```cpp
constexpr explicit vector(const Allocator&) noexcept;
```

**1** *Effects:* Constructs an empty `vector`, using the specified allocator.

**2** *Complexity:* Constant.

```cpp
constexpr explicit vector(size_type n, const Allocator& = Allocator());
```

**3** *Preconditions:* `T` is *Cpp17DefaultInsertable* into `*this`.

**4** *Effects:* Constructs a `vector` with `n` default-inserted elements using the specified allocator.

**5** *Complexity:* Linear in `n`.

```cpp
constexpr vector(size_type n, const T& value,
                 const Allocator& = Allocator());
```

**6** *Preconditions:* `T` is *Cpp17CopyInsertable* into `*this`.

**7** *Effects:* Constructs a `vector` with `n` copies of `value`, using the specified allocator.

**8** *Complexity:* Linear in `n`.

```cpp
template<class InputIterator>
  constexpr vector(InputIterator first, InputIterator last,
                   const Allocator& = Allocator());
```

**9** *Effects:* Constructs a `vector` equal to the range `[first, last)`, using the specified allocator.

**10** *Complexity:* Makes only *N* calls to the copy constructor of `T` (where *N* is the distance between `first` and `last`) and no reallocations if iterators `first` and `last` are of forward, bidirectional, or random access categories. It makes order `N` calls to the copy constructor of `T` and order log *N* reallocations if they are just input iterators.
