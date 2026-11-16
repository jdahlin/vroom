# 18.4.2 Concept `same_as` [concept.same]

[Contents](../../README.md) · up: [18.4 Language-related concepts \[concepts.lang\]](concepts.lang.md) · prev: [18.4.1 General \[concepts.lang.general\]](concepts.lang.general.md) · next: [18.4.3 Concept `derived_from` \[concept.derived\]](concept.derived.md)

```cpp
template<class T, class U>
  concept same-as-impl = is_same_v<T, U>;       // exposition only

template<class T, class U>
  concept same_as = same-as-impl<T, U> && same-as-impl<U, T>;
```

**1** [*Note 1*: `same_as<T, U>` subsumes `same_as<U, T>` and vice versa. — *end note*]
