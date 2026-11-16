# 20.5.7 Element access [tuple.elem]

[Contents](../../README.md) · up: [20.5 Tuples \[tuple\]](tuple.md) · prev: [20.5.6 Tuple helper classes \[tuple.helper\]](tuple.helper.md) · next: [20.5.8 Relational operators \[tuple.rel\]](tuple.rel.md)

```cpp
template<size_t I, class... Types>
  constexpr tuple_element_t<I, tuple<Types...>>&
    get(tuple<Types...>& t) noexcept;
template<size_t I, class... Types>
  constexpr tuple_element_t<I, tuple<Types...>>&&
    get(tuple<Types...>&& t) noexcept;        // Note A
template<size_t I, class... Types>
  constexpr const tuple_element_t<I, tuple<Types...>>&
    get(const tuple<Types...>& t) noexcept;   // Note B
template<size_t I, class... Types>
  constexpr const tuple_element_t<I, tuple<Types...>>&& get(const tuple<Types...>&& t) noexcept;
```

**1** *Mandates:* `I` &lt; `sizeof...(Types)`.

**2** *Returns:* A reference to the `I`<sup></sup>*th* element of `t`, where indexing is zero-based.

**3** [*Note 1*: [Note A] If a type `T` in `Types` is some reference type `X&`, the return type is `X&`, not `X&&`. However, if the element type is a non-reference type `T`, the return type is `T&&`. — *end note*]

**4** [*Note 2*: [Note B] Constness is shallow. If a type `T` in `Types` is some reference type `X&`, the return type is `X&`, not `const X&`. However, if the element type is a non-reference type `T`, the return type is `const T&`. This is consistent with how constness is defined to work for member variables of reference type. — *end note*]

```cpp
template<class T, class... Types>
  constexpr T& get(tuple<Types...>& t) noexcept;
template<class T, class... Types>
  constexpr T&& get(tuple<Types...>&& t) noexcept;
template<class T, class... Types>
  constexpr const T& get(const tuple<Types...>& t) noexcept;
template<class T, class... Types>
  constexpr const T&& get(const tuple<Types...>&& t) noexcept;
```

**5** *Mandates:* The type `T` occurs exactly once in `Types`.

**6** *Returns:* A reference to the element of `t` corresponding to the type `T` in `Types`.

**7** [*Example 1*:

```cpp
const tuple<int, const int, double, double> t(1, 2, 3.4, 5.6);
const int& i1 = get<int>(t);                    // OK, i1 has value 1
const int& i2 = get<const int>(t);              // OK, i2 has value 2
const double& d = get<double>(t);               // error: type double is not unique within t
```

— *end example*]

**8** [*Note 3*: The reason `get` is a non-member function is that if this functionality had been provided as a member function, code where the type depended on a template parameter would have required using the `template` keyword. — *end note*]
