# 17.11.6 Comparison algorithms [cmp.alg]

[Contents](../README.md) · up: [17.11 Comparisons \[cmp\]](cmp.md) · prev: [17.11.5 Result of three-way comparison \[cmp.result\]](cmp.result.md) · next: [17.12 Coroutines \[support.coroutine\]](support.coroutine.md)

**1** The name `strong_order` denotes a customization point object ([[customization.point.object]](customization.point.object.md)). Given subexpressions `E` and `F`, the expression `strong_order(E, F)` is expression-equivalent ([[defns.expression-equivalent]](definitions.md#defns.expression-equivalent)) to the following:

- If the decayed types of `E` and `F` differ, `strong_order(E, F)` is ill-formed.
- Otherwise, `strong_ordering(strong_order(E, F))` if it is a well-formed expression with overload resolution performed in a context that does not include a declaration of `std::strong_order`.
- Otherwise, if the decayed type `T` of `E` is a floating-point type, yields a value of type `strong_ordering` that is consistent with the ordering observed by `T`’s comparison operators, and if `numeric_limits<T>::is_iec559` is `true`, is additionally consistent with the `totalOrder` operation as specified in ISO/IEC/IEEE 60559.
- Otherwise, `strong_ordering(compare_three_way()(E, F))` if it is a well-formed expression.
- Otherwise, `strong_order(E, F)` is ill-formed. [*Note 1*: This case can result in substitution failure when `strong_order(E, F)` appears in the immediate context of a template instantiation. — *end note*]

**2** The name `weak_order` denotes a customization point object ([[customization.point.object]](customization.point.object.md)). Given subexpressions `E` and `F`, the expression `weak_order(E, F)` is expression-equivalent ([[defns.expression-equivalent]](definitions.md#defns.expression-equivalent)) to the following:

- If the decayed types of `E` and `F` differ, `weak_order(E, F)` is ill-formed.

- Otherwise, `weak_ordering(weak_order(E, F))` if it is a well-formed expression with overload resolution performed in a context that does not include a declaration of `std::weak_order`.

- Otherwise, if the decayed type `T` of `E` is a floating-point type, yields a value of type `weak_ordering` that is consistent with the ordering observed by `T`’s comparison operators and `strong_order`, and if `numeric_limits<T>::is_iec559` is `true`, is additionally consistent with the following equivalence classes, ordered from lesser to greater:

  - together, all negative NaN values;
  - negative infinity;
  - each normal negative value;
  - each subnormal negative value;
  - together, both zero values;
  - each subnormal positive value;
  - each normal positive value;
  - positive infinity;
  - together, all positive NaN values.

- Otherwise, `weak_ordering(compare_three_way()(E, F))` if it is a well-formed expression.

- Otherwise, `weak_ordering(strong_order(E, F))` if it is a well-formed expression.

- Otherwise, `weak_order(E, F)` is ill-formed. [*Note 2*: This case can result in substitution failure when `std::weak_order(E, F)` appears in the immediate context of a template instantiation. — *end note*]

**3** The name `partial_order` denotes a customization point object ([[customization.point.object]](customization.point.object.md)). Given subexpressions `E` and `F`, the expression `partial_order(E, F)` is expression-equivalent ([[defns.expression-equivalent]](definitions.md#defns.expression-equivalent)) to the following:

- If the decayed types of `E` and `F` differ, `partial_order(E, F)` is ill-formed.
- Otherwise, `partial_ordering(partial_order(E, F))` if it is a well-formed expression with overload resolution performed in a context that does not include a declaration of `std::partial_order`.
- Otherwise, `partial_ordering(compare_three_way()(E, F))` if it is a well-formed expression.
- Otherwise, `partial_ordering(weak_order(E, F))` if it is a well-formed expression.
- Otherwise, `partial_order(E, F)` is ill-formed. [*Note 3*: This case can result in substitution failure when `std::partial_order(E, F)` appears in the immediate context of a template instantiation. — *end note*]

**4** The name `compare_strong_order_fallback` denotes a customization point object ([[customization.point.object]](customization.point.object.md)). Given subexpressions `E` and F, the expression `compare_strong_order_fallback(E, F)` is expression-equivalent ([[defns.expression-equivalent]](definitions.md#defns.expression-equivalent)) to:

- If the decayed types of `E` and `F` differ, `compare_strong_order_fallback(E, F)` is ill-formed.

- Otherwise, `strong_order(E, F)` if it is a well-formed expression.

- Otherwise, if the expressions `E == F` and `E < F` are both well-formed and convertible to `bool`,

  ```cpp
  E == F ? strong_ordering::equal :
  E < F  ? strong_ordering::less :
           strong_ordering::greater
  ```

  except that `E` and `F` are evaluated only once.

- Otherwise, `compare_strong_order_fallback(E, F)` is ill-formed.

**5** The name `compare_weak_order_fallback` denotes a customization point object ([[customization.point.object]](customization.point.object.md)). Given subexpressions `E` and `F`, the expression `compare_weak_order_fallback(E, F)` is expression-equivalent ([[defns.expression-equivalent]](definitions.md#defns.expression-equivalent)) to:

- If the decayed types of `E` and `F` differ, `compare_weak_order_fallback(E, F)` is ill-formed.

- Otherwise, `weak_order(E, F)` if it is a well-formed expression.

- Otherwise, if the expressions `E == F` and `E < F` are both well-formed and convertible to `bool`,

  ```cpp
  E == F ? weak_ordering::equivalent :
  E < F  ? weak_ordering::less :
           weak_ordering::greater
  ```

  except that `E` and `F` are evaluated only once.

- Otherwise, `compare_weak_order_fallback(E, F)` is ill-formed.

**6** The name `compare_partial_order_fallback` denotes a customization point object ([[customization.point.object]](customization.point.object.md)). Given subexpressions `E` and `F`, the expression `compare_partial_order_fallback(E, F)` is expression-equivalent ([[defns.expression-equivalent]](definitions.md#defns.expression-equivalent)) to:

- If the decayed types of `E` and `F` differ, `compare_partial_order_fallback(E, F)` is ill-formed.

- Otherwise, `partial_order(E, F)` if it is a well-formed expression.

- Otherwise, if the expressions `E == F` and `E < F` are both well-formed and convertible to `bool`,

  ```cpp
  E == F ? partial_ordering::equivalent :
  E < F  ? partial_ordering::less :
  F < E  ? partial_ordering::greater :
           partial_ordering::unordered
  ```

  except that `E` and `F` are evaluated only once.

- Otherwise, `compare_partial_order_fallback(E, F)` is ill-formed.
