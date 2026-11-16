# 22.4.7.3 Erasure [multiset.erasure]

[Contents](../README.md) · up: [22.4.7 Class template `multiset` \[multiset\]](multiset.md) · prev: [22.4.7.2 Constructors \[multiset.cons\]](multiset.cons.md) · next: [22.5 Unordered associative containers \[unord\]](unord.md)

```cpp
template<class Key, class Compare, class Allocator, class Predicate>
  typename multiset<Key, Compare, Allocator>::size_type
    erase_if(multiset<Key, Compare, Allocator>& c, Predicate pred);
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
