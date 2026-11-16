# 22.4.4.5 Erasure [map.erasure]

[Contents](../README.md) · up: [22.4.4 Class template `map` \[map\]](map.md) · prev: [22.4.4.4 Modifiers \[map.modifiers\]](map.modifiers.md) · next: [22.4.5 Class template `multimap` \[multimap\]](multimap.md)

```cpp
template<class Key, class T, class Compare, class Allocator, class Predicate>
  typename map<Key, T, Compare, Allocator>::size_type
    erase_if(map<Key, T, Compare, Allocator>& c, Predicate pred);
```

**1** *Effects:* Equivalent to:

```cpp
auto original_size = c.size();
for (auto i = c.begin(), last = c.end(); i != last; ) {
  if (pred(*i)) {
    i = c.erase(i);
  } else {
    ++i;
  }
}
return original_size - c.size();
```
