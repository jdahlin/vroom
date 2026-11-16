# 22.3.10.4 Modifiers [list.modifiers]

[Contents](../../README.md) · up: [22.3.10 Class template `list` \[list\]](list.md) · prev: [22.3.10.3 Capacity \[list.capacity\]](list.capacity.md) · next: [22.3.10.5 Operations \[list.ops\]](list.ops.md)

```cpp
iterator insert(const_iterator position, const T& x);
iterator insert(const_iterator position, T&& x);
iterator insert(const_iterator position, size_type n, const T& x);
template<class InputIterator>
  iterator insert(const_iterator position, InputIterator first,
                  InputIterator last);
iterator insert(const_iterator position, initializer_list<T>);

template<class... Args> reference emplace_front(Args&&... args);
template<class... Args> reference emplace_back(Args&&... args);
template<class... Args> iterator emplace(const_iterator position, Args&&... args);
void push_front(const T& x);
void push_front(T&& x);
void push_back(const T& x);
void push_back(T&& x);
```

**1** *Remarks:* Does not affect the validity of iterators and references. If an exception is thrown there are no effects.

**2** *Complexity:* Insertion of a single element into a list takes constant time and exactly one call to a constructor of `T`. Insertion of multiple elements into a list is linear in the number of elements inserted, and the number of calls to the copy constructor or move constructor of `T` is exactly equal to the number of elements inserted.

```cpp
iterator erase(const_iterator position);
iterator erase(const_iterator first, const_iterator last);

void pop_front();
void pop_back();
void clear() noexcept;
```

**3** *Effects:* Invalidates only the iterators and references to the erased elements.

**4** *Throws:* Nothing.

**5** *Complexity:* Erasing a single element is a constant time operation with a single call to the destructor of `T`. Erasing a range in a list is linear time in the size of the range and the number of calls to the destructor of type `T` is exactly equal to the size of the range.
