# 22.3.9.7 Erasure [forward.list.erasure]

[Contents](../README.md) · up: [22.3.9 Class template `forward_list` \[forwardlist\]](forwardlist.md) · prev: [22.3.9.6 Operations \[forwardlist.ops\]](forwardlist.ops.md) · next: [22.3.10 Class template `list` \[list\]](list.md)

```cpp
template<class T, class Allocator, class U>
  typename forward_list<T, Allocator>::size_type
    erase(forward_list<T, Allocator>& c, const U& value);
```

**1** *Effects:* Equivalent to: `return erase_if(c, [&](auto& elem) { return elem == value; });`

```cpp
template<class T, class Allocator, class Predicate>
  typename forward_list<T, Allocator>::size_type
    erase_if(forward_list<T, Allocator>& c, Predicate pred);
```

**2** *Effects:* Equivalent to: `return c.remove_if(pred);`
