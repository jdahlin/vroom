# 22.5.6.3 Erasure [unord.set.erasure]

[Contents](../../README.md) · up: [22.5.6 Class template `unordered_set` \[unord.set\]](unord.set.md) · prev: [22.5.6.2 Constructors \[unord.set.cnstr\]](unord.set.cnstr.md) · next: [22.5.7 Class template `unordered_multiset` \[unord.multiset\]](unord.multiset.md)

```cpp
template<class K, class H, class P, class A, class Predicate>
  typename unordered_set<K, H, P, A>::size_type
    erase_if(unordered_set<K, H, P, A>& c, Predicate pred);
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
