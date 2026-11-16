# 20.14.7 Comparisons [comparisons]

[Contents](../README.md) · up: [20.14 Function objects \[function.objects\]](function.objects.md) · prev: [20.14.6.6 Class template `negate` \[arithmetic.operations.negate\]](arithmetic.operations.negate.md) · next: [20.14.7.1 Class template `equal_to` \[comparisons.equal.to\]](comparisons.equal.to.md)

**1** The library provides basic function object classes for all of the comparison operators in the language ([[expr.rel]](../expr.rel.md), [[expr.eq]](../expr.eq.md)).

**2** For templates `less`, `greater`, `less_equal`, and `greater_equal`, the specializations for any pointer type yield a result consistent with the implementation-defined strict total order over pointers ([[defns.order.ptr]](definitions.md#defns.order.ptr)). [*Note 1*: If `a < b` is well-defined for pointers `a` and `b` of type `P`, then `(a < b) == less<P>()(a, b)`, `(a > b) == greater<P>()(a, b)`, and so forth. — *end note*] For template specializations `less<void>`, `greater<void>`, `less_equal<void>`, and `greater_equal<void>`, if the call operator calls a built-in operator comparing pointers, the call operator yields a result consistent with the implementation-defined strict total order over pointers.

## Subclauses

- [20.14.7.1 Class template `equal_to` \[comparisons.equal.to\]](comparisons.equal.to.md)
- [20.14.7.2 Class template `not_equal_to` \[comparisons.not.equal.to\]](comparisons.not.equal.to.md)
- [20.14.7.3 Class template `greater` \[comparisons.greater\]](comparisons.greater.md)
- [20.14.7.4 Class template `less` \[comparisons.less\]](comparisons.less.md)
- [20.14.7.5 Class template `greater_equal` \[comparisons.greater.equal\]](comparisons.greater.equal.md)
- [20.14.7.6 Class template `less_equal` \[comparisons.less.equal\]](comparisons.less.equal.md)
- [20.14.7.7 Class `compare_three_way` \[comparisons.three.way\]](comparisons.three.way.md)
