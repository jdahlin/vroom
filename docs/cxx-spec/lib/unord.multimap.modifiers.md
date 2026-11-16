# 22.5.5.3 Modifiers [unord.multimap.modifiers]

[Contents](../README.md) · up: [22.5.5 Class template `unordered_multimap` \[unord.multimap\]](unord.multimap.md) · prev: [22.5.5.2 Constructors \[unord.multimap.cnstr\]](unord.multimap.cnstr.md) · next: [22.5.5.4 Erasure \[unord.multimap.erasure\]](unord.multimap.erasure.md)

```cpp
template<class P>
  iterator insert(P&& obj);
```

**1** *Constraints:* `is_constructible_v<value_type, P&&>` is `true`.

**2** *Effects:* Equivalent to: `return emplace(std::forward<P>(obj));`

```cpp
template<class P>
  iterator insert(const_iterator hint, P&& obj);
```

**3** *Constraints:* `is_constructible_v<value_type, P&&>` is `true`.

**4** *Effects:* Equivalent to: `return emplace_hint(hint, std::forward<P>(obj));`
