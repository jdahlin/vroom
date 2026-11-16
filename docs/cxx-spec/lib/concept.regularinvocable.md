# 18.7.3 Concept `regular_invocable` [concept.regularinvocable]

[Contents](../README.md) · up: [18.7 Callable concepts \[concepts.callable\]](concepts.callable.md) · prev: [18.7.2 Concept `invocable` \[concept.invocable\]](concept.invocable.md) · next: [18.7.4 Concept `predicate` \[concept.predicate\]](concept.predicate.md)

```cpp
template<class F, class... Args>
  concept regular_invocable = invocable<F, Args...>;
```

**1** The `invoke` function call expression shall be equality-preserving ([[concepts.equality]](concepts.equality.md)) and shall not modify the function object or the arguments. [*Note 1*: This requirement supersedes the annotation in the definition of `invocable`. — *end note*]

**2** [*Example 1*: A random number generator does not model `regular_invocable`. — *end example*]

**3** [*Note 2*: The distinction between `invocable` and `regular_invocable` is purely semantic. — *end note*]
