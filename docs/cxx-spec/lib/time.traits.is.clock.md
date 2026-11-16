# 27.4.4 Class template `is_clock` [time.traits.is.clock]

[Contents](../README.md) · up: [27.4 Time-related traits \[time.traits\]](time.traits.md) · prev: [27.4.3 Specializations of `common_type` \[time.traits.specializations\]](time.traits.specializations.md) · next: [27.5 Class template `duration` \[time.duration\]](time.duration.md)

```cpp
template<class T> struct is_clock;
```

**1** `is_clock` is a *Cpp17UnaryTypeTrait* ([[meta.rqmts]](meta.rqmts.md)) with a base characteristic of `true_type` if `T` meets the *Cpp17Clock* requirements ([[time.clock.req]](time.clock.req.md)), otherwise `false_type`. For the purposes of the specification of this trait, the extent to which an implementation determines that a type cannot meet the *Cpp17Clock* requirements is unspecified, except that as a minimum a type `T` shall not qualify as a *Cpp17Clock* unless it meets all of the following conditions:

- the *qualified-id*s `T::rep`, `T::period`, `T::duration`, and `T::time_point` are valid and each denotes a type ([[temp.deduct]](../temp.deduct.md)),
- the expression `T::is_steady` is well-formed when treated as an unevaluated operand,
- the expression `T::now()` is well-formed when treated as an unevaluated operand.

**2** The behavior of a program that adds specializations for `is_clock` is undefined.
