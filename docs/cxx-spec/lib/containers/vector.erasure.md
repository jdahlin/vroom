# 22.3.11.6 Erasure [vector.erasure]

[Contents](../../README.md) · up: [22.3.11 Class template `vector` \[vector\]](vector.md) · prev: [22.3.11.5 Modifiers \[vector.modifiers\]](vector.modifiers.md) · next: [22.3.12 Class `vector<bool>` \[vector.bool\]](vector.bool.md)

```cpp
template<class T, class Allocator, class U>
  constexpr typename vector<T, Allocator>::size_type
    erase(vector<T, Allocator>& c, const U& value);
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
  constexpr typename vector<T, Allocator>::size_type
    erase_if(vector<T, Allocator>& c, Predicate pred);
```

**2** *Effects:* Equivalent to:

```cpp
auto it = remove_if(c.begin(), c.end(), pred);
auto r = distance(it, c.end());
c.erase(it, c.end());
return r;
```
