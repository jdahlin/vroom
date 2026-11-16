# 20.7.4 `variant` helper classes [variant.helper]

[Contents](../../README.md) · up: [20.7 Variants \[variant\]](variant.md) · prev: [20.7.3.6 Swap \[variant.swap\]](variant.swap.md) · next: [20.7.5 Value access \[variant.get\]](variant.get.md)

```cpp
template<class T> struct variant_size;
```

**1** All specializations of `variant_size` meet the *Cpp17UnaryTypeTrait* requirements ([[meta.rqmts]](meta.rqmts.md)) with a base characteristic of `integral_constant<size_t, N>` for some `N`.

```cpp
template<class T> class variant_size<const T>;
```

**2** Let `VS` denote `variant_size<T>` of the cv-unqualified type `T`. Then each specialization of the template meets the *Cpp17UnaryTypeTrait* requirements ([[meta.rqmts]](meta.rqmts.md)) with a base characteristic of `integral_constant<size_t, VS::value>`.

```cpp
template<class... Types>
  struct variant_size<variant<Types...>> : integral_constant<size_t, sizeof...(Types)> { };
```

```cpp
template<size_t I, class T> class variant_alternative<I, const T>;
```

**3** Let `VA` denote `variant_alternative<I, T>` of the cv-unqualified type `T`. Then each specialization of the template meets the *Cpp17TransformationTrait* requirements ([[meta.rqmts]](meta.rqmts.md)) with a member typedef `type` that names the type `add_const_t<VA::type>`.

```cpp
variant_alternative<I, variant<Types...>>::type
```

**4** *Mandates:* `I` &lt; `sizeof...(Types)`.

**5** *Type:* The type `T`<sub>*I*</sub>.
