# 18.4.10 Concept `destructible` [concept.destructible]

[Contents](../README.md) · up: [18.4 Language-related concepts \[concepts.lang\]](concepts.lang.md) · prev: [18.4.9 Concept `swappable` \[concept.swappable\]](concept.swappable.md) · next: [18.4.11 Concept `constructible_from` \[concept.constructible\]](concept.constructible.md)

**1** The `destructible` concept specifies properties of all types, instances of which can be destroyed at the end of their lifetime, or reference types.

```cpp
template<class T>
  concept destructible = is_nothrow_destructible_v<T>;
```

**2** [*Note 1*: Unlike the *Cpp17Destructible* requirements ([Table 32](utility.arg.requirements.md#tab:cpp17.destructible)), this concept forbids destructors that are potentially throwing, even if a particular invocation of the destructor does not actually throw. — *end note*]
