# 20.7.12 Hash support [variant.hash]

[Contents](../README.md) · up: [20.7 Variants \[variant\]](variant.md) · prev: [20.7.11 Class `bad_variant_access` \[variant.bad.access\]](variant.bad.access.md) · next: [20.8 Storage for any type \[any\]](any.md)

```cpp
template<class... Types> struct hash<variant<Types...>>;
```

**1** The specialization `hash<variant<Types...>>` is enabled ([[unord.hash]](unord.hash.md)) if and only if every specialization in `hash<remove_const_t<Types>>...` is enabled. The member functions are not guaranteed to be `noexcept`.

```cpp
template<> struct hash<monostate>;
```

**2** The specialization is enabled ([[unord.hash]](unord.hash.md)).
