# 20.17.4 Hash support [type.index.hash]

[Contents](../README.md) · up: [20.17 Class `type_index` \[type.index\]](type.index.md) · prev: [20.17.3 `type_index` members \[type.index.members\]](type.index.members.md) · next: [20.18 Execution policies \[execpol\]](execpol.md)

```cpp
template<> struct hash<type_index>;
```

**1** For an object `index` of type `type_index`, `hash<type_index>()(index)` shall evaluate to the same result as `index.hash_code()`.
