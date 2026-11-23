# D.13 Deprecated type traits [depr.meta.types]

[Contents](../README.md) · up: [Annex D Compatibility features \[depr\]](depr.md) · prev: [D.12.5.3 `strstream` operations \[depr.strstream.oper\]](depr.strstream.oper.md) · next: [D.14 Tuple \[depr.tuple\]](depr.tuple.md)

**1** The header `<type_traits>` ([[meta.type.synop]](../lib/utilities/meta.type.synop.md)) has the following addition:

```cpp
namespace std {
  template<class T> struct is_pod;
  template<class T> inline constexpr bool is_pod_v = is_pod<T>::value;
}
```

**2** The behavior of a program that adds specializations for any of the templates defined in this subclause is undefined, unless explicitly permitted by the specification of the corresponding template.

```cpp
template<class T> struct is_pod;
```

**3** *Requires:* `remove_all_extents_t<T>` shall be a complete type or *cv* `void`.

**4** `is_pod<T>` is a *Cpp17UnaryTypeTrait* ([[meta.rqmts]](../lib/utilities/meta.rqmts.md)) with a base characteristic of `true_type` if `T` is a POD type, and `false_type` otherwise. A POD class is a class that is both a trivial class and a standard-layout class, and has no non-static data members of type non-POD class (or array thereof). A POD type is a scalar type, a POD class, an array of such a type, or a cv-qualified version of one of these types.

**5** [*Note 1*: It is unspecified whether a closure type ([[expr.prim.lambda.closure]](../expr/expr.prim.lambda.closure.md)) is a POD type. — *end note*]
