# 20.5.8 Relational operators [tuple.rel]

[Contents](../../README.md) · up: [20.5 Tuples \[tuple\]](tuple.md) · prev: [20.5.7 Element access \[tuple.elem\]](tuple.elem.md) · next: [20.5.9 Tuple traits \[tuple.traits\]](tuple.traits.md)

```cpp
template<class... TTypes, class... UTypes>
  constexpr bool operator==(const tuple<TTypes...>& t, const tuple<UTypes...>& u);
```

**1** *Mandates:* For all `i`, where 0 ≤ `i` &lt; `sizeof...(TTypes)`, `get<i>(t) == get<i>(u)` is a valid expression returning a type that is convertible to `bool`. `sizeof...(TTypes)` equals `sizeof...(UTypes)`.

**2** *Returns:* `true` if `get<i>(t) == get<i>(u)` for all `i`, otherwise `false`. For any two zero-length tuples `e` and `f`, `e == f` returns `true`.

**3** *Effects:* The elementary comparisons are performed in order from the zeroth index upwards. No comparisons or element accesses are performed after the first equality comparison that evaluates to `false`.

```cpp
template<class... TTypes, class... UTypes>
  constexpr common_comparison_category_t<synth-three-way-result<TTypes, UTypes>...>
    operator<=>(const tuple<TTypes...>& t, const tuple<UTypes...>& u);
```

**4** *Effects:* Performs a lexicographical comparison between `t` and `u`. For any two zero-length tuples `t` and `u`, `t <=> u` returns `strong_ordering::equal`. Otherwise, equivalent to:

```cpp
if (auto c = synth-three-way(get<0>(t), get<0>(u)); c != 0) return c;
return t_()tail <=> u_()tail;
```

where `r`<sub></sub>*tail* for some tuple `r` is a tuple containing all but the first element of `r`.

**5** [*Note 1*: The above definition does not require `t_(tail)` (or `u_(tail)`) to be constructed. It may not even be possible, as `t` and `u` are not required to be copy constructible. Also, all comparison functions are short circuited; they do not perform element accesses beyond what is required to determine the result of the comparison. — *end note*]
