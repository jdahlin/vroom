# 23.3.7.2 Concept `indirectly_movable` [alg.req.ind.move]

[Contents](../README.md) · up: [23.3.7 Common algorithm requirements \[alg.req\]](alg.req.md) · prev: [23.3.7.1 General \[alg.req.general\]](alg.req.general.md) · next: [23.3.7.3 Concept `indirectly_copyable` \[alg.req.ind.copy\]](alg.req.ind.copy.md)

**1** The `indirectly_movable` concept specifies the relationship between a `indirectly_readable` type and a `indirectly_writable` type between which values may be moved.

```cpp
template<class In, class Out>
  concept indirectly_movable =
    indirectly_readable<In> &&
    indirectly_writable<Out, iter_rvalue_reference_t<In>>;
```

**2** The `indirectly_movable_storable` concept augments `indirectly_movable` with additional requirements enabling the transfer to be performed through an intermediate object of the `indirectly_readable` type’s value type.

```cpp
template<class In, class Out>
  concept indirectly_movable_storable =
    indirectly_movable<In, Out> &&
    indirectly_writable<Out, iter_value_t<In>> &&
    movable<iter_value_t<In>> &&
    constructible_from<iter_value_t<In>, iter_rvalue_reference_t<In>> &&
    assignable_from<iter_value_t<In>&, iter_rvalue_reference_t<In>>;
```

**3** Let `i` be a dereferenceable value of type `In`. `In` and `Out` model `indirectly_movable_storable<In, Out>` only if after the initialization of the object `obj` in

```cpp
iter_value_t<In> obj(ranges::iter_move(i));
```

`obj` is equal to the value previously denoted by `*i`. If `iter_rvalue_reference_t<In>` is an rvalue reference type, the resulting state of the value denoted by `*i` is valid but unspecified ([[lib.types.movedfrom]](lib.types.movedfrom.md)).
