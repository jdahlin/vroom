# 22.5.5.4 Erasure [unord.multimap.erasure]

[Contents](../../README.md) · up: [22.5.5 Class template `unordered_multimap` \[unord.multimap\]](unord.multimap.md) · prev: [22.5.5.3 Modifiers \[unord.multimap.modifiers\]](unord.multimap.modifiers.md) · next: [22.5.6 Class template `unordered_set` \[unord.set\]](unord.set.md)

```cpp
template<class K, class T, class H, class P, class A, class Predicate>
  typename unordered_multimap<K, T, H, P, A>::size_type
    erase_if(unordered_multimap<K, T, H, P, A>& c, Predicate pred);
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
