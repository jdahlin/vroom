# 18.7.4 Concept `predicate` [concept.predicate]

[Contents](../README.md) · up: [18.7 Callable concepts \[concepts.callable\]](concepts.callable.md) · prev: [18.7.3 Concept `regular_invocable` \[concept.regularinvocable\]](concept.regularinvocable.md) · next: [18.7.5 Concept `relation` \[concept.relation\]](concept.relation.md)

```cpp
template<class F, class... Args>
  concept predicate =
    regular_invocable<F, Args...> && boolean-testable<invoke_result_t<F, Args...>>;
```
