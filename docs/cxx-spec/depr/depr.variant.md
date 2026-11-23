# D.15 Variant [depr.variant]

[Contents](../README.md) · up: [Annex D Compatibility features \[depr\]](depr.md) · prev: [D.14 Tuple \[depr.tuple\]](depr.tuple.md) · next: [D.16 Deprecated iterator primitives \[depr.iterator.primitives\]](depr.iterator.primitives.md)

**1** The header `<variant>` ([[variant.syn]](../lib/utilities/variant.syn.md)) has the following additions:

```cpp
namespace std {
  template<class T> struct variant_size<volatile T>;
  template<class T> struct variant_size<const volatile T>;

  template<size_t I, class T> struct variant_alternative<I, volatile T>;
  template<size_t I, class T> struct variant_alternative<I, const volatile T>;
}
```

```cpp
template<class T> class variant_size<volatile T>;
template<class T> class variant_size<const volatile T>;
```

**2** Let `VS` denote `variant_size<T>` of the cv-unqualified type `T`. Then specializations of each of the two templates meet the *Cpp17UnaryTypeTrait* requirements with a base characteristic of `integral_constant<size_t, VS::value>`.

```cpp
template<size_t I, class T> class variant_alternative<I, volatile T>;
template<size_t I, class T> class variant_alternative<I, const volatile T>;
```

**3** Let `VA` denote `variant_alternative<I, T>` of the cv-unqualified type `T`. Then specializations of each of the two templates meet the *Cpp17TransformationTrait* requirements with a member typedef `type` that names the following type:

- for the first specialization, `add_volatile_t<VA::type>`, and
- for the second specialization, `add_cv_t<VA::type>`.
