# 22.3.10.3 Capacity [list.capacity]

[Contents](../../README.md) · up: [22.3.10 Class template `list` \[list\]](list.md) · prev: [22.3.10.2 Constructors, copy, and assignment \[list.cons\]](list.cons.md) · next: [22.3.10.4 Modifiers \[list.modifiers\]](list.modifiers.md)

```cpp
void resize(size_type sz);
```

**1** *Preconditions:* `T` is *Cpp17DefaultInsertable* into `*this`.

**2** *Effects:* If `size() < sz`, appends `sz - size()` default-inserted elements to the sequence. If `sz <= size()`, equivalent to:

```cpp
list<T>::iterator it = begin();
advance(it, sz);
erase(it, end());
```

```cpp
void resize(size_type sz, const T& c);
```

**3** *Preconditions:* `T` is *Cpp17CopyInsertable* into `*this`.

**4** *Effects:* As if by:

```cpp
if (sz > size())
  insert(end(), sz-size(), c);
else if (sz < size()) {
  iterator i = begin();
  advance(i, sz);
  erase(i, end());
}
else
  ;                 // do nothing
```
