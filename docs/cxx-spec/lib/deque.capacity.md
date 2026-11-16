# 22.3.8.3 Capacity [deque.capacity]

[Contents](../README.md) · up: [22.3.8 Class template `deque` \[deque\]](deque.md) · prev: [22.3.8.2 Constructors, copy, and assignment \[deque.cons\]](deque.cons.md) · next: [22.3.8.4 Modifiers \[deque.modifiers\]](deque.modifiers.md)

```cpp
void resize(size_type sz);
```

**1** *Preconditions:* `T` is *Cpp17MoveInsertable* and *Cpp17DefaultInsertable* into `*this`.

**2** *Effects:* If `sz < size()`, erases the last `size() - sz` elements from the sequence. Otherwise, appends `sz - size()` default-inserted elements to the sequence.

```cpp
void resize(size_type sz, const T& c);
```

**3** *Preconditions:* `T` is *Cpp17CopyInsertable* into `*this`.

**4** *Effects:* If `sz < size()`, erases the last `size() - sz` elements from the sequence. Otherwise, appends `sz - size()` copies of `c` to the sequence.

```cpp
void shrink_to_fit();
```

**5** *Preconditions:* `T` is *Cpp17MoveInsertable* into `*this`.

**6** *Effects:* `shrink_to_fit` is a non-binding request to reduce memory use but does not change the size of the sequence. [*Note 1*: The request is non-binding to allow latitude for implementation-specific optimizations. — *end note*] If the size is equal to the old capacity, or if an exception is thrown other than by the move constructor of a non-*Cpp17CopyInsertable* `T`, then there are no effects.

**7** *Complexity:* If the size is not equal to the old capacity, linear in the size of the sequence; otherwise constant.

**8** *Remarks:* If the size is not equal to the old capacity, then invalidates all the references, pointers, and iterators referring to the elements in the sequence, as well as the past-the-end iterator.
