# 20.5.6 Tuple helper classes [tuple.helper]

[Contents](../../README.md) · up: [20.5 Tuples \[tuple\]](tuple.md) · prev: [20.5.5 Calling a function with a `tuple` of arguments \[tuple.apply\]](tuple.apply.md) · next: [20.5.7 Element access \[tuple.elem\]](tuple.elem.md)

```cpp
template<class T> struct tuple_size;
```

**1** All specializations of `tuple_size` meet the *Cpp17UnaryTypeTrait* requirements ([[meta.rqmts]](meta.rqmts.md)) with a base characteristic of `integral_constant<size_t, N>` for some `N`.

```cpp
template<class... Types>
  struct tuple_size<tuple<Types...>> : public integral_constant<size_t, sizeof...(Types)> { };
```

```cpp
template<size_t I, class... Types>
  struct tuple_element<I, tuple<Types...>> {
    using type = TI;
  };
```

**2** *Mandates:* `I` &lt; `sizeof...(Types)`.

**3** *Type:* `TI` is the type of the `I`<sup></sup>*th* element of `Types`, where indexing is zero-based.

```cpp
template<class T> struct tuple_size<const T>;
```

**4** Let `TS` denote `tuple_size<T>` of the cv-unqualified type `T`. If the expression `TS::value` is well-formed when treated as an unevaluated operand, then each specialization of the template meets the *Cpp17UnaryTypeTrait* requirements ([[meta.rqmts]](meta.rqmts.md)) with a base characteristic of

```cpp
integral_constant<size_t, TS::value>
```

Otherwise, it has no member `value`.

**5** Access checking is performed as if in a context unrelated to `TS` and `T`. Only the validity of the immediate context of the expression is considered. [*Note 1*: The compilation of the expression can result in side effects such as the instantiation of class template specializations and function template specializations, the generation of implicitly-defined functions, and so on. Such side effects are not in the “immediate context” and can result in the program being ill-formed. — *end note*]

**6** In addition to being available via inclusion of the `<tuple>` header, the template is available when any of the headers `<array>` ([[array.syn]](../containers/array.syn.md)), `<ranges>` ([[ranges.syn]](../ranges/ranges.syn.md)), or `<utility>` ([[utility.syn]](utility.syn.md)) are included.

```cpp
template<size_t I, class T> struct tuple_element<I, const T>;
```

**7** Let `TE` denote `tuple_element_t<I, T>` of the cv-unqualified type `T`. Then each specialization of the template meets the *Cpp17TransformationTrait* requirements ([[meta.rqmts]](meta.rqmts.md)) with a member typedef `type` that names the type `add_const_t<TE>`.

**8** In addition to being available via inclusion of the `<tuple>` header, the template is available when any of the headers `<array>` ([[array.syn]](../containers/array.syn.md)), `<ranges>` ([[ranges.syn]](../ranges/ranges.syn.md)), or `<utility>` ([[utility.syn]](utility.syn.md)) are included.
