# 23.3.6.2 Indirect callables [indirectcallable.indirectinvocable]

[Contents](../README.md) · up: [23.3.6 Indirect callable requirements \[indirectcallable\]](indirectcallable.md) · prev: [23.3.6.1 General \[indirectcallable.general\]](indirectcallable.general.md) · next: [23.3.6.3 Class template `projected` \[projected\]](projected.md)

**1** The indirect callable concepts are used to constrain those algorithms that accept callable objects ([[func.def]](func.def.md)) as arguments.

```cpp
namespace std {
  template<class F, class I>
    concept indirectly_unary_invocable =
      indirectly_readable<I> &&
      copy_constructible<F> &&
      invocable<F&, iter_value_t<I>&> &&
      invocable<F&, iter_reference_t<I>> &&
      invocable<F&, iter_common_reference_t<I>> &&
      common_reference_with<
        invoke_result_t<F&, iter_value_t<I>&>,
        invoke_result_t<F&, iter_reference_t<I>>>;

  template<class F, class I>
    concept indirectly_regular_unary_invocable =
      indirectly_readable<I> &&
      copy_constructible<F> &&
      regular_invocable<F&, iter_value_t<I>&> &&
      regular_invocable<F&, iter_reference_t<I>> &&
      regular_invocable<F&, iter_common_reference_t<I>> &&
      common_reference_with<
        invoke_result_t<F&, iter_value_t<I>&>,
        invoke_result_t<F&, iter_reference_t<I>>>;

  template<class F, class I>
    concept indirect_unary_predicate =
      indirectly_readable<I> &&
      copy_constructible<F> &&
      predicate<F&, iter_value_t<I>&> &&
      predicate<F&, iter_reference_t<I>> &&
      predicate<F&, iter_common_reference_t<I>>;

  template<class F, class I1, class I2>
    concept indirect_binary_predicate =
      indirectly_readable<I1> && indirectly_readable<I2> &&
      copy_constructible<F> &&
      predicate<F&, iter_value_t<I1>&, iter_value_t<I2>&> &&
      predicate<F&, iter_value_t<I1>&, iter_reference_t<I2>> &&
      predicate<F&, iter_reference_t<I1>, iter_value_t<I2>&> &&
      predicate<F&, iter_reference_t<I1>, iter_reference_t<I2>> &&
      predicate<F&, iter_common_reference_t<I1>, iter_common_reference_t<I2>>;

  template<class F, class I1, class I2 = I1>
    concept indirect_equivalence_relation =
      indirectly_readable<I1> && indirectly_readable<I2> &&
      copy_constructible<F> &&
      equivalence_relation<F&, iter_value_t<I1>&, iter_value_t<I2>&> &&
      equivalence_relation<F&, iter_value_t<I1>&, iter_reference_t<I2>> &&
      equivalence_relation<F&, iter_reference_t<I1>, iter_value_t<I2>&> &&
      equivalence_relation<F&, iter_reference_t<I1>, iter_reference_t<I2>> &&
      equivalence_relation<F&, iter_common_reference_t<I1>, iter_common_reference_t<I2>>;

  template<class F, class I1, class I2 = I1>
    concept indirect_strict_weak_order =
      indirectly_readable<I1> && indirectly_readable<I2> &&
      copy_constructible<F> &&
      strict_weak_order<F&, iter_value_t<I1>&, iter_value_t<I2>&> &&
      strict_weak_order<F&, iter_value_t<I1>&, iter_reference_t<I2>> &&
      strict_weak_order<F&, iter_reference_t<I1>, iter_value_t<I2>&> &&
      strict_weak_order<F&, iter_reference_t<I1>, iter_reference_t<I2>> &&
      strict_weak_order<F&, iter_common_reference_t<I1>, iter_common_reference_t<I2>>;
}
```
