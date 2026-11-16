# 22.5.4.5 Erasure [unord.map.erasure]

[Contents](../README.md) · up: [22.5.4 Class template `unordered_map` \[unord.map\]](unord.map.md) · prev: [22.5.4.4 Modifiers \[unord.map.modifiers\]](unord.map.modifiers.md) · next: [22.5.5 Class template `unordered_multimap` \[unord.multimap\]](unord.multimap.md)

```cpp
template<class K, class T, class H, class P, class A, class Predicate>
  typename unordered_map<K, T, H, P, A>::size_type
    erase_if(unordered_map<K, T, H, P, A>& c, Predicate pred);
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
