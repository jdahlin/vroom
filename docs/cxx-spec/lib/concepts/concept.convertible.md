# 18.4.4 Concept `convertible_to` [concept.convertible]

[Contents](../../README.md) · up: [18.4 Language-related concepts \[concepts.lang\]](concepts.lang.md) · prev: [18.4.3 Concept `derived_from` \[concept.derived\]](concept.derived.md) · next: [18.4.5 Concept `common_reference_with` \[concept.commonref\]](concept.commonref.md)

**1** Given types `From` and `To` and an expression `E` such that `decltype((E))` is `add_rvalue_reference_t<From>`, `convertible_to<From, To>` requires `E` to be both implicitly and explicitly convertible to type `To`. The implicit and explicit conversions are required to produce equal results.

```cpp
template<class From, class To>
  concept convertible_to =
    is_convertible_v<From, To> &&
    requires(add_rvalue_reference_t<From> (&f)()) {
      static_cast<To>(f());
    };
```

**2** Let `FromR` be `add_rvalue_reference_t<From>` and `test` be the invented function:

```cpp
To test(FromR (&f)()) {
  return f();
}
```

and let `f` be a function with no arguments and return type `FromR` such that `f()` is equality-preserving. Types `From` and `To` model `convertible_to<From, To>` only if:

- `To` is not an object or reference-to-object type, or `static_cast<To>(f())` is equal to `test(f)`.

- `FromR` is not a reference-to-object type, or

  - If `FromR` is an rvalue reference to a non const-qualified type, the resulting state of the object referenced by `f()` after either above expression is valid but unspecified ([[lib.types.movedfrom]](../library/lib.types.movedfrom.md)).
  - Otherwise, the object referred to by `f()` is not modified by either above expression.
