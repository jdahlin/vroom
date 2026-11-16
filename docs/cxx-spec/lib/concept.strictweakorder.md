# 18.7.7 Concept `strict_weak_order` [concept.strictweakorder]

[Contents](../README.md) · up: [18.7 Callable concepts \[concepts.callable\]](concepts.callable.md) · prev: [18.7.6 Concept `equivalence_relation` \[concept.equiv\]](concept.equiv.md) · next: [19 Diagnostics library \[diagnostics\]](diagnostics.md)

```cpp
template<class R, class T, class U>
  concept strict_weak_order = relation<R, T, U>;
```

**1** A `relation` models `strict_weak_order` only if it imposes a *strict weak ordering* on its arguments.

**2** The term *strict* refers to the requirement of an irreflexive relation (`!comp(x, x)` for all `x`), and the term *weak* to requirements that are not as strong as those for a total ordering, but stronger than those for a partial ordering. If we define `equiv(a, b)` as `!comp(a, b) && !comp(b, a)`, then the requirements are that `comp` and `equiv` both be transitive relations:

- `comp(a, b) && comp(b, c)` implies `comp(a, c)`
- `equiv(a, b) && equiv(b, c)` implies `equiv(a, c)`

**3** [*Note 1*: Under these conditions, it can be shown that

- `equiv` is an equivalence relation,
- `comp` induces a well-defined relation on the equivalence classes determined by `equiv`, and
- the induced relation is a strict total ordering.

— *end note*]
