# 23.3.7.3 Concept `indirectly_copyable` [alg.req.ind.copy]

[Contents](../README.md) · up: [23.3.7 Common algorithm requirements \[alg.req\]](alg.req.md) · prev: [23.3.7.2 Concept `indirectly_movable` \[alg.req.ind.move\]](alg.req.ind.move.md) · next: [23.3.7.4 Concept `indirectly_swappable` \[alg.req.ind.swap\]](alg.req.ind.swap.md)

**1** The `indirectly_copyable` concept specifies the relationship between a `indirectly_readable` type and a `indirectly_writable` type between which values may be copied.

```cpp
template<class In, class Out>
  concept indirectly_copyable =
    indirectly_readable<In> &&
    indirectly_writable<Out, iter_reference_t<In>>;
```

**2** The `indirectly_copyable_storable` concept augments `indirectly_copyable` with additional requirements enabling the transfer to be performed through an intermediate object of the `indirectly_readable` type’s value type. It also requires the capability to make copies of values.

```cpp
template<class In, class Out>
  concept indirectly_copyable_storable =
    indirectly_copyable<In, Out> &&
    indirectly_writable<Out, iter_value_t<In>&> &&
    indirectly_writable<Out, const iter_value_t<In>&> &&
    indirectly_writable<Out, iter_value_t<In>&&> &&
    indirectly_writable<Out, const iter_value_t<In>&&> &&
    copyable<iter_value_t<In>> &&
    constructible_from<iter_value_t<In>, iter_reference_t<In>> &&
    assignable_from<iter_value_t<In>&, iter_reference_t<In>>;
```

**3** Let `i` be a dereferenceable value of type `In`. `In` and `Out` model `indirectly_copyable_storable<In, Out>` only if after the initialization of the object `obj` in

```cpp
iter_value_t<In> obj(*i);
```

`obj` is equal to the value previously denoted by `*i`. If `iter_reference_t<In>` is an rvalue reference type, the resulting state of the value denoted by `*i` is valid but unspecified ([[lib.types.movedfrom]](lib.types.movedfrom.md)).
