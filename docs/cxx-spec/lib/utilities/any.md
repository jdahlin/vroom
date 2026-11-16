# 20.8 Storage for any type [any]

[Contents](../../README.md) · up: [20 General utilities library \[utilities\]](utilities.md) · prev: [20.7.12 Hash support \[variant.hash\]](variant.hash.md) · next: [20.8.1 Header `<any>` synopsis \[any.synop\]](any.synop.md)

**1** This subclause describes components that C++ programs may use to perform operations on objects of a discriminated type.

**2** [*Note 1*: The discriminated type may contain values of different types but does not attempt conversion between them, i.e., `5` is held strictly as an `int` and is not implicitly convertible either to `"5"` or to `5.0`. This indifference to interpretation but awareness of type effectively allows safe, generic containers of single values, with no scope for surprises from ambiguous conversions. — *end note*]

## Subclauses

- [20.8.1 Header `<any>` synopsis \[any.synop\]](any.synop.md)
- [20.8.2 Class `bad_any_cast` \[any.bad.any.cast\]](any.bad.any.cast.md)
- [20.8.3 Class `any` \[any.class\]](any.class.md)
  - [20.8.3.1 Construction and destruction \[any.cons\]](any.cons.md)
  - [20.8.3.2 Assignment \[any.assign\]](any.assign.md)
  - [20.8.3.3 Modifiers \[any.modifiers\]](any.modifiers.md)
  - [20.8.3.4 Observers \[any.observers\]](any.observers.md)
- [20.8.4 Non-member functions \[any.nonmembers\]](any.nonmembers.md)
