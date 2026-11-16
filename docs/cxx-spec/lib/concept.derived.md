# 18.4.3 Concept `derived_from` [concept.derived]

[Contents](../README.md) · up: [18.4 Language-related concepts \[concepts.lang\]](concepts.lang.md) · prev: [18.4.2 Concept `same_as` \[concept.same\]](concept.same.md) · next: [18.4.4 Concept `convertible_to` \[concept.convertible\]](concept.convertible.md)

```cpp
template<class Derived, class Base>
  concept derived_from =
    is_base_of_v<Base, Derived> &&
    is_convertible_v<const volatile Derived*, const volatile Base*>;
```

**1** [*Note 1*: `derived_from<Derived, Base>` is satisfied if and only if `Derived` is publicly and unambiguously derived from `Base`, or `Derived` and `Base` are the same class type ignoring cv-qualifiers. — *end note*]
