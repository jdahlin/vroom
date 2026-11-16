# 22.3.11.3 Capacity [vector.capacity]

[Contents](../../README.md) · up: [22.3.11 Class template `vector` \[vector\]](vector.md) · prev: [22.3.11.2 Constructors, copy, and assignment \[vector.cons\]](vector.cons.md) · next: [22.3.11.4 Data \[vector.data\]](vector.data.md)

```cpp
constexpr size_type capacity() const noexcept;
```

**1** *Returns:* The total number of elements that the vector can hold without requiring reallocation.

**2** *Complexity:* Constant time.

```cpp
constexpr void reserve(size_type n);
```

**3** *Preconditions:* `T` is *Cpp17MoveInsertable* into `*this`.

**4** *Effects:* A directive that informs a `vector` of a planned change in size, so that it can manage the storage allocation accordingly. After `reserve()`, `capacity()` is greater or equal to the argument of `reserve` if reallocation happens; and equal to the previous value of `capacity()` otherwise. Reallocation happens at this point if and only if the current capacity is less than the argument of `reserve()`. If an exception is thrown other than by the move constructor of a non-*Cpp17CopyInsertable* type, there are no effects.

**5** *Complexity:* It does not change the size of the sequence and takes at most linear time in the size of the sequence.

**6** *Throws:* `length_error` if `n > max_size()`.[^1]

**7** *Remarks:* Reallocation invalidates all the references, pointers, and iterators referring to the elements in the sequence, as well as the past-the-end iterator. [*Note 1*: If no reallocation happens, they remain valid. — *end note*] No reallocation shall take place during insertions that happen after a call to `reserve()` until an insertion would make the size of the vector greater than the value of `capacity()`.

```cpp
constexpr void shrink_to_fit();
```

**8** *Preconditions:* `T` is *Cpp17MoveInsertable* into `*this`.

**9** *Effects:* `shrink_to_fit` is a non-binding request to reduce `capacity()` to `size()`. [*Note 2*: The request is non-binding to allow latitude for implementation-specific optimizations. — *end note*] It does not increase `capacity()`, but may reduce `capacity()` by causing reallocation. If an exception is thrown other than by the move constructor of a non-*Cpp17CopyInsertable* `T` there are no effects.

**10** *Complexity:* If reallocation happens, linear in the size of the sequence.

**11** *Remarks:* Reallocation invalidates all the references, pointers, and iterators referring to the elements in the sequence as well as the past-the-end iterator. [*Note 3*: If no reallocation happens, they remain valid. — *end note*]

```cpp
constexpr void swap(vector& x)
  noexcept(allocator_traits<Allocator>::propagate_on_container_swap::value ||
           allocator_traits<Allocator>::is_always_equal::value);
```

**12** *Effects:* Exchanges the contents and `capacity()` of `*this` with that of `x`.

**13** *Complexity:* Constant time.

```cpp
constexpr void resize(size_type sz);
```

**14** *Preconditions:* `T` is *Cpp17MoveInsertable* and *Cpp17DefaultInsertable* into `*this`.

**15** *Effects:* If `sz < size()`, erases the last `size() - sz` elements from the sequence. Otherwise, appends `sz - size()` default-inserted elements to the sequence.

**16** *Remarks:* If an exception is thrown other than by the move constructor of a non-*Cpp17CopyInsertable* `T` there are no effects.

```cpp
constexpr void resize(size_type sz, const T& c);
```

**17**

**18** *Preconditions:* `T` is *Cpp17CopyInsertable* into `*this`.

*Effects:* If `sz < size()`, erases the last `size() - sz` elements from the sequence. Otherwise, appends `sz - size()` copies of `c` to the sequence.

**19** *Remarks:* If an exception is thrown there are no effects.

[^1]: `reserve()` uses `Allocator::allocate()` which may throw an appropriate exception.
