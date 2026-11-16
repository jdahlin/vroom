# 22.3.10.6 Erasure [list.erasure]

[Contents](../../README.md) · up: [22.3.10 Class template `list` \[list\]](list.md) · prev: [22.3.10.5 Operations \[list.ops\]](list.ops.md) · next: [22.3.11 Class template `vector` \[vector\]](vector.md)

```cpp
template<class T, class Allocator, class U>
  typename list<T, Allocator>::size_type
    erase(list<T, Allocator>& c, const U& value);
```

**1** *Effects:* Equivalent to: `return erase_if(c, [&](auto& elem) { return elem == value; });`

```cpp
template<class T, class Allocator, class Predicate>
  typename list<T, Allocator>::size_type
    erase_if(list<T, Allocator>& c, Predicate pred);
```

**2** *Effects:* Equivalent to: `return c.remove_if(pred);`
