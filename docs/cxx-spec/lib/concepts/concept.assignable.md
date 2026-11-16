# 18.4.8 Concept `assignable_from` [concept.assignable]

[Contents](../../README.md) · up: [18.4 Language-related concepts \[concepts.lang\]](concepts.lang.md) · prev: [18.4.7 Arithmetic concepts \[concepts.arithmetic\]](concepts.arithmetic.md) · next: [18.4.9 Concept `swappable` \[concept.swappable\]](concept.swappable.md)

```cpp
template<class LHS, class RHS>
  concept assignable_from =
    is_lvalue_reference_v<LHS> &&
    common_reference_with<const remove_reference_t<LHS>&, const remove_reference_t<RHS>&> &&
    requires(LHS lhs, RHS&& rhs) {
      { lhs = std::forward<RHS>(rhs) } -> same_as<LHS>;
    };
```

**1** Let:

- `lhs` be an lvalue that refers to an object `lcopy` such that `decltype((lhs))` is `LHS`,
- `rhs` be an expression such that `decltype((rhs))` is `RHS`, and
- `rcopy` be a distinct object that is equal to `rhs`.

`LHS` and `RHS` model `assignable_from<LHS, RHS>` only if

- `addressof(lhs = rhs) == addressof(lcopy)`.

- After evaluating `lhs = rhs`:

  - `lhs` is equal to `rcopy`, unless `rhs` is a non-const xvalue that refers to `lcopy`.
  - If `rhs` is a non-`const` xvalue, the resulting state of the object to which it refers is valid but unspecified ([[lib.types.movedfrom]](../library/lib.types.movedfrom.md)).
  - Otherwise, if `rhs` is a glvalue, the object to which it refers is not modified.

**2** [*Note 1*: Assignment need not be a total function ([[structure.requirements]](../library/structure.requirements.md)); in particular, if assignment to an object `x` can result in a modification of some other object `y`, then `x = y` is likely not in the domain of `=`. — *end note*]
