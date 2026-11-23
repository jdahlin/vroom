# 18.7.6 Concept `equivalence_relation` [concept.equiv]

[Contents](../../README.md) · up: [18.7 Callable concepts \[concepts.callable\]](concepts.callable.md) · prev: [18.7.5 Concept `relation` \[concept.relation\]](concept.relation.md) · next: [18.7.7 Concept `strict_weak_order` \[concept.strictweakorder\]](concept.strictweakorder.md)

```cpp
template<class R, class T, class U>
  concept equivalence_relation = relation<R, T, U>;
```

**1** A `relation` models `equivalence_relation` only if it imposes an equivalence relation on its arguments.
