# 22.4.5.3 Modifiers [multimap.modifiers]

[Contents](../README.md) · up: [22.4.5 Class template `multimap` \[multimap\]](multimap.md) · prev: [22.4.5.2 Constructors \[multimap.cons\]](multimap.cons.md) · next: [22.4.5.4 Erasure \[multimap.erasure\]](multimap.erasure.md)

```cpp
template<class P> iterator insert(P&& x);
template<class P> iterator insert(const_iterator position, P&& x);
```

**1** *Constraints:* `is_constructible_v<value_type, P&&>` is `true`.

**2** *Effects:* The first form is equivalent to `return emplace(std::forward<P>(x))`. The second form is equivalent to `return emplace_hint(position, std::forward<P>(x))`.
