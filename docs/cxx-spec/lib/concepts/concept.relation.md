# 18.7.5 Concept `relation` [concept.relation]

[Contents](../../README.md) · up: [18.7 Callable concepts \[concepts.callable\]](concepts.callable.md) · prev: [18.7.4 Concept `predicate` \[concept.predicate\]](concept.predicate.md) · next: [18.7.6 Concept `equivalence_relation` \[concept.equiv\]](concept.equiv.md)

```cpp
template<class R, class T, class U>
  concept relation =
    predicate<R, T, T> && predicate<R, U, U> &&
    predicate<R, T, U> && predicate<R, U, T>;
```
