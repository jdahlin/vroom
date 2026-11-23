# 22.4.6.3 Erasure [set.erasure]

[Contents](../../README.md) · up: [22.4.6 Class template `set` \[set\]](set.md) · prev: [22.4.6.2 Constructors, copy, and assignment \[set.cons\]](set.cons.md) · next: [22.4.7 Class template `multiset` \[multiset\]](multiset.md)

```cpp
template<class Key, class Compare, class Allocator, class Predicate>
  typename set<Key, Compare, Allocator>::size_type
    erase_if(set<Key, Compare, Allocator>& c, Predicate pred);
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
