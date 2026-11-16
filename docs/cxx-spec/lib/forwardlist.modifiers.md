# 22.3.9.5 Modifiers [forwardlist.modifiers]

[Contents](../README.md) · up: [22.3.9 Class template `forward_list` \[forwardlist\]](forwardlist.md) · prev: [22.3.9.4 Element access \[forwardlist.access\]](forwardlist.access.md) · next: [22.3.9.6 Operations \[forwardlist.ops\]](forwardlist.ops.md)

**1** None of the overloads of `insert_after` shall affect the validity of iterators and references, and `erase_after` shall invalidate only iterators and references to the erased elements. If an exception is thrown during `insert_after` there shall be no effect. Inserting `n` elements into a `forward_list` is linear in `n`, and the number of calls to the copy or move constructor of `T` is exactly equal to `n`. Erasing `n` elements from a `forward_list` is linear in `n` and the number of calls to the destructor of type `T` is exactly equal to `n`.

```cpp
template<class... Args> reference emplace_front(Args&&... args);
```

**2** *Effects:* Inserts an object of type `value_type` constructed with `value_type(std::forward<Args>(args)...)` at the beginning of the list.

```cpp
void push_front(const T& x);
void push_front(T&& x);
```

**3** *Effects:* Inserts a copy of `x` at the beginning of the list.

```cpp
void pop_front();
```

**4** *Effects:* As if by `erase_after(before_begin())`.

```cpp
iterator insert_after(const_iterator position, const T& x);
iterator insert_after(const_iterator position, T&& x);
```

**5** *Preconditions:* `position` is `before_begin()` or is a dereferenceable iterator in the range `[begin(), end())`.

**6** *Effects:* Inserts a copy of `x` after `position`.

**7** *Returns:* An iterator pointing to the copy of `x`.

```cpp
iterator insert_after(const_iterator position, size_type n, const T& x);
```

**8** *Preconditions:* `position` is `before_begin()` or is a dereferenceable iterator in the range `[begin(), end())`.

**9** *Effects:* Inserts `n` copies of `x` after `position`.

**10** *Returns:* An iterator pointing to the last inserted copy of `x` or `position` if `n == 0`.

```cpp
template<class InputIterator>
  iterator insert_after(const_iterator position, InputIterator first, InputIterator last);
```

**11** *Preconditions:* `position` is `before_begin()` or is a dereferenceable iterator in the range `[begin(), end())`. Neither `first` nor `last` are iterators in `*this`.

**12** *Effects:* Inserts copies of elements in `[first, last)` after `position`.

**13** *Returns:* An iterator pointing to the last inserted element or `position` if `first == last`.

```cpp
iterator insert_after(const_iterator position, initializer_list<T> il);
```

**14** *Effects:* `insert_after(p, il.begin(), il.end())`.

**15** *Returns:* An iterator pointing to the last inserted element or `position` if `il` is empty.

```cpp
template<class... Args>
  iterator emplace_after(const_iterator position, Args&&... args);
```

**16** *Preconditions:* `position` is `before_begin()` or is a dereferenceable iterator in the range `[begin(), end())`.

**17** *Effects:* Inserts an object of type `value_type` constructed with `value_type(std::forward<Args>(args)...)` after `position`.

**18** *Returns:* An iterator pointing to the new object.

```cpp
iterator erase_after(const_iterator position);
```

**19** *Preconditions:* The iterator following `position` is dereferenceable.

**20** *Effects:* Erases the element pointed to by the iterator following `position`.

**21** *Returns:* An iterator pointing to the element following the one that was erased, or `end()` if no such element exists.

**22** *Throws:* Nothing.

```cpp
iterator erase_after(const_iterator position, const_iterator last);
```

**23** *Preconditions:* All iterators in the range `(position, last)` are dereferenceable.

**24** *Effects:* Erases the elements in the range `(position, last)`.

**25** *Returns:* `last`.

**26** *Throws:* Nothing.

```cpp
void resize(size_type sz);
```

**27** *Preconditions:* `T` is *Cpp17DefaultInsertable* into `*this`.

**28** *Effects:* If `sz < distance(begin(), end())`, erases the last `distance(begin(), end()) - sz` elements from the list. Otherwise, inserts `sz - distance(begin(), end())` default-inserted elements at the end of the list.

```cpp
void resize(size_type sz, const value_type& c);
```

**29** *Preconditions:* `T` is *Cpp17CopyInsertable* into `*this`.

**30** *Effects:* If `sz < distance(begin(), end())`, erases the last `distance(begin(), end()) - sz` elements from the list. Otherwise, inserts `sz - distance(begin(), end())` copies of `c` at the end of the list.

```cpp
void clear() noexcept;
```

**31** *Effects:* Erases all elements in the range `[begin(), end())`.

**32** *Remarks:* Does not invalidate past-the-end iterators.
