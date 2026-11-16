# 22.3.8.5 Erasure [deque.erasure]

[Contents](../README.md) · up: [22.3.8 Class template `deque` \[deque\]](deque.md) · prev: [22.3.8.4 Modifiers \[deque.modifiers\]](deque.modifiers.md) · next: [22.3.9 Class template `forward_list` \[forwardlist\]](forwardlist.md)

```cpp
template<class T, class Allocator, class U>
  typename deque<T, Allocator>::size_type
    erase(deque<T, Allocator>& c, const U& value);
```

**1** *Effects:* Equivalent to:

```cpp
auto it = remove(c.begin(), c.end(), value);
auto r = distance(it, c.end());
c.erase(it, c.end());
return r;
```

```cpp
template<class T, class Allocator, class Predicate>
  typename deque<T, Allocator>::size_type
    erase_if(deque<T, Allocator>& c, Predicate pred);
```

**2** *Effects:* Equivalent to:

```cpp
auto it = remove_if(c.begin(), c.end(), pred);
auto r = distance(it, c.end());
c.erase(it, c.end());
return r;
```
