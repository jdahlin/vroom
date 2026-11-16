# 20.5.4 Tuple creation functions [tuple.creation]

[Contents](../README.md) · up: [20.5 Tuples \[tuple\]](tuple.md) · prev: [20.5.3.3 `swap` \[tuple.swap\]](tuple.swap.md) · next: [20.5.5 Calling a function with a `tuple` of arguments \[tuple.apply\]](tuple.apply.md)

**1** In the function descriptions that follow, the members of a template parameter pack `XTypes` are denoted by `X`<sub>*i*</sub> for *i* in `[0, sizeof...(XTypes))` in order, where indexing is zero-based.

```cpp
template<class... TTypes>
  constexpr tuple<unwrap_ref_decay_t<TTypes>...> make_tuple(TTypes&&... t);
```

**2** *Returns:* `tuple<unwrap_ref_decay_t<TTypes>...>(std::forward<TTypes>(t)...)`.

**3** [*Example 1*:

```cpp
int i; float j;
make_tuple(1, ref(i), cref(j))
```

creates a tuple of type `tuple<int, int&, const float&>`. — *end example*]

```cpp
template<class... TTypes>
  constexpr tuple<TTypes&&...> forward_as_tuple(TTypes&&... t) noexcept;
```

**4** *Effects:* Constructs a tuple of references to the arguments in `t` suitable for forwarding as arguments to a function. Because the result may contain references to temporary objects, a program shall ensure that the return value of this function does not outlive any of its arguments (e.g., the program should typically not store the result in a named variable).

**5** *Returns:* `tuple<TTypes&&...>(std::forward<TTypes>(t)...)`.

```cpp
template<class... TTypes>
  constexpr tuple<TTypes&...> tie(TTypes&... t) noexcept;
```

**6** *Returns:* `tuple<TTypes&...>(t...)`. When an argument in `t` is `ignore`, assigning any value to the corresponding tuple element has no effect.

**7** [*Example 2*: `tie` functions allow one to create tuples that unpack tuples into variables. `ignore` can be used for elements that are not needed:

```cpp
int i; std::string s;
tie(i, ignore, s) = make_tuple(42, 3.14, "C++");
// i == 42, s == "C++"
```

— *end example*]

```cpp
template<class... Tuples>
  constexpr tuple<CTypes...> tuple_cat(Tuples&&... tpls);
```

**8** In the following paragraphs, let `T`<sub>*i*</sub> be the *i*<sup></sup>*th* type in `Tuples`, `U`<sub>*i*</sub> be `remove_reference_t<T_i>`, and `tp`<sub>*i*</sub> be the *i*<sup></sup>*th* parameter in the function parameter pack `tpls`, where all indexing is zero-based.

**9** *Preconditions:* For all *i*, `U`<sub>*i*</sub> is the type *cv*<sub>*i*</sub> `tuple<Args_i...>`, where *cv*<sub>*i*</sub> is the (possibly empty) *i*<sup></sup>*th* *cv-qualifier-seq* and `Args`<sub>*i*</sub> is the template parameter pack representing the element types in `U`<sub>*i*</sub>. Let `A`<sub>*ik*</sub> be the *k*<sup></sup>*th* type in `Args`<sub>*i*</sub>. For all `A`<sub>*ik*</sub> the following requirements are met:

- If `T`<sub>*i*</sub> is deduced as an lvalue reference type, then `is_constructible_v<A_(ik), cv_i A_(ik)&> == true`, otherwise
- `is_constructible_v<A_(ik), cv_i A_(ik)&&> == true`.

**10** *Remarks:* The types in `CTypes` are equal to the ordered sequence of the extended types `Args_0..., Args_1..., ..., Args_(n-1)...`, where *n* is equal to `sizeof...(Tuples)`. Let `e_i...` be the *i*<sup></sup>*th* ordered sequence of tuple elements of the resulting `tuple` object corresponding to the type sequence `Args`<sub>*i*</sub>.

**11** *Returns:* A `tuple` object constructed by initializing the *k*<sub>*i*</sub><sup></sup>*th* type element `e`<sub>*ik*</sub> in `e_i...` with

```cpp
get<k_i>(std::forward<T_i>(tp_i))
```

for each valid *k*<sub>*i*</sub> and each group `e`<sub>*i*</sub> in order.

**12** [*Note 1*: An implementation may support additional types in the template parameter pack `Tuples` that support the `tuple`-like protocol, such as `pair` and `array`. — *end note*]
