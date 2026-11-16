# 23.3.3.1 `ranges::iter_move` [iterator.cust.move]

[Contents](../README.md) · up: [23.3.3 Customization points \[iterator.cust\]](iterator.cust.md) · prev: [23.3.3 Customization points \[iterator.cust\]](iterator.cust.md) · next: [23.3.3.2 `ranges::iter_swap` \[iterator.cust.swap\]](iterator.cust.swap.md)

**1** The name `ranges::iter_move` denotes a customization point object ([[customization.point.object]](customization.point.object.md)). The expression `ranges::iter_move(E)` for a subexpression `E` is expression-equivalent to:

- `iter_move(E)`, if `E` has class or enumeration type and `iter_move(E)` is a well-formed expression when treated as an unevaluated operand, with overload resolution performed in a context that does not include a declaration of `ranges::iter_move` but does include the declaration

  ```cpp
  void iter_move();
  ```

- Otherwise, if the expression `*E` is well-formed:

  - if `*E` is an lvalue, `std::move(*E)`;
  - otherwise, `*E`.

- Otherwise, `ranges::iter_move(E)` is ill-formed. [*Note 1*: This case can result in substitution failure when `ranges::iter_move(E)` appears in the immediate context of a template instantiation. — *end note*]

**2** If `ranges::iter_move(E)` is not equal to `*E`, the program is ill-formed, no diagnostic required.
