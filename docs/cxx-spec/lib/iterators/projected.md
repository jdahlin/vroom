# 23.3.6.3 Class template `projected` [projected]

[Contents](../../README.md) · up: [23.3.6 Indirect callable requirements \[indirectcallable\]](indirectcallable.md) · prev: [23.3.6.2 Indirect callables \[indirectcallable.indirectinvocable\]](indirectcallable.indirectinvocable.md) · next: [23.3.7 Common algorithm requirements \[alg.req\]](alg.req.md)

**1** Class template `projected` is used to constrain algorithms that accept callable objects and projections ([[defns.projection]](../library/definitions.md#defns.projection)). It combines a `indirectly_readable` type `I` and a callable object type `Proj` into a new `indirectly_readable` type whose `reference` type is the result of applying `Proj` to the `iter_reference_t` of `I`.

```cpp
namespace std {
  template<indirectly_readable I, indirectly_regular_unary_invocable<I> Proj>
  struct projected {
    using value_type = remove_cvref_t<indirect_result_t<Proj&, I>>;
    indirect_result_t<Proj&, I> operator*() const;              // not defined
  };

  template<weakly_incrementable I, class Proj>
  struct incrementable_traits<projected<I, Proj>> {
    using difference_type = iter_difference_t<I>;
  };
}
```
