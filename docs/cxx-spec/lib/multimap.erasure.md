# 22.4.5.4 Erasure [multimap.erasure]

[Contents](../README.md) · up: [22.4.5 Class template `multimap` \[multimap\]](multimap.md) · prev: [22.4.5.3 Modifiers \[multimap.modifiers\]](multimap.modifiers.md) · next: [22.4.6 Class template `set` \[set\]](set.md)

```cpp
template<class Key, class T, class Compare, class Allocator, class Predicate>
  typename multimap<Key, T, Compare, Allocator>::size_type
    erase_if(multimap<Key, T, Compare, Allocator>& c, Predicate pred);
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
