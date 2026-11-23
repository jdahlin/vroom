# D.14 Tuple [depr.tuple]

[Contents](../README.md) · up: [Annex D Compatibility features \[depr\]](depr.md) · prev: [D.13 Deprecated type traits \[depr.meta.types\]](depr.meta.types.md) · next: [D.15 Variant \[depr.variant\]](depr.variant.md)

The header `<tuple>` ([[tuple.syn]](../lib/utilities/tuple.syn.md)) has the following additions:

```cpp
namespace std {
  template<class T> class tuple_size<volatile T>;
  template<class T> class tuple_size<const volatile T>;

  template<size_t I, class T> class tuple_element<I, volatile T>;
  template<size_t I, class T> class tuple_element<I, const volatile T>;
}
```

```cpp
template<class T> class tuple_size<volatile T>;
template<class T> class tuple_size<const volatile T>;
```

**1** Let `TS` denote `tuple_size<T>` of the cv-unqualified type `T`. If the expression `TS::value` is well-formed when treated as an unevaluated operand, then specializations of each of the two templates meet the *Cpp17TransformationTrait* requirements with a base characteristic of `integral_constant<size_t, TS::value>`. Otherwise, they have no member `value`.

**2** Access checking is performed as if in a context unrelated to `TS` and `T`. Only the validity of the immediate context of the expression is considered.

**3** In addition to being available via inclusion of the `<tuple>` ([[tuple.syn]](../lib/utilities/tuple.syn.md)) header, the two templates are available when any of the headers `<array>` ([[array.syn]](../lib/containers/array.syn.md)), `<ranges>` ([[ranges.syn]](../lib/ranges/ranges.syn.md)), or `<utility>` ([[utility.syn]](../lib/utilities/utility.syn.md)) are included.

```cpp
template<size_t I, class T> class tuple_element<I, volatile T>;
template<size_t I, class T> class tuple_element<I, const volatile T>;
```

**4** Let `TE` denote `tuple_element_t<I, T>` of the cv-unqualified type `T`. Then specializations of each of the two templates meet the *Cpp17TransformationTrait* requirements with a member typedef `type` that names the following type:

- for the first specialization, `add_volatile_t<TE>`, and
- for the second specialization, `add_cv_t<TE>`.

**5** In addition to being available via inclusion of the `<tuple>` ([[tuple.syn]](../lib/utilities/tuple.syn.md)) header, the two templates are available when any of the headers `<array>` ([[array.syn]](../lib/containers/array.syn.md)), `<ranges>` ([[ranges.syn]](../lib/ranges/ranges.syn.md)), or `<utility>` ([[utility.syn]](../lib/utilities/utility.syn.md)) are included.
