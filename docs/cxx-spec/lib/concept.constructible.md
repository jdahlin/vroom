# 18.4.11 Concept `constructible_from` [concept.constructible]

[Contents](../README.md) · up: [18.4 Language-related concepts \[concepts.lang\]](concepts.lang.md) · prev: [18.4.10 Concept `destructible` \[concept.destructible\]](concept.destructible.md) · next: [18.4.12 Concept `default_initializable` \[concept.default.init\]](concept.default.init.md)

**1** The `constructible_from` concept constrains the initialization of a variable of a given type with a particular set of argument types.

```cpp
template<class T, class... Args>
  concept constructible_from = destructible<T> && is_constructible_v<T, Args...>;
```
