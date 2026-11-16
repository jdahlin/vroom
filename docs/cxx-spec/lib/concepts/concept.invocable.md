# 18.7.2 Concept `invocable` [concept.invocable]

[Contents](../../README.md) · up: [18.7 Callable concepts \[concepts.callable\]](concepts.callable.md) · prev: [18.7.1 General \[concepts.callable.general\]](concepts.callable.general.md) · next: [18.7.3 Concept `regular_invocable` \[concept.regularinvocable\]](concept.regularinvocable.md)

**1** The `invocable` concept specifies a relationship between a callable type ([[func.def]](../utilities/func.def.md)) `F` and a set of argument types `Args...` which can be evaluated by the library function `invoke` ([[func.invoke]](../utilities/func.invoke.md)).

```cpp
template<class F, class... Args>
  concept invocable = requires(F&& f, Args&&... args) {
    invoke(std::forward<F>(f), std::forward<Args>(args)...); // not required to be equality-preserving
  };
```

**2** [*Example 1*: A function that generates random numbers can model `invocable`, since the `invoke` function call expression is not required to be equality-preserving ([[concepts.equality]](concepts.equality.md)). — *end example*]
