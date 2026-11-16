# 22.5.7.3 Erasure [unord.multiset.erasure]

[Contents](../../README.md) · up: [22.5.7 Class template `unordered_multiset` \[unord.multiset\]](unord.multiset.md) · prev: [22.5.7.2 Constructors \[unord.multiset.cnstr\]](unord.multiset.cnstr.md) · next: [22.6 Container adaptors \[container.adaptors\]](container.adaptors.md)

```cpp
template<class K, class H, class P, class A, class Predicate>
  typename unordered_multiset<K, H, P, A>::size_type
    erase_if(unordered_multiset<K, H, P, A>& c, Predicate pred);
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
