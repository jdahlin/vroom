# 20.7.5 Value access [variant.get]

[Contents](../README.md) · up: [20.7 Variants \[variant\]](variant.md) · prev: [20.7.4 `variant` helper classes \[variant.helper\]](variant.helper.md) · next: [20.7.6 Relational operators \[variant.relops\]](variant.relops.md)

```cpp
template<class T, class... Types>
  constexpr bool holds_alternative(const variant<Types...>& v) noexcept;
```

**1** *Mandates:* The type `T` occurs exactly once in `Types`.

**2** *Returns:* `true` if `index()` is equal to the zero-based index of `T` in `Types`.

```cpp
template<size_t I, class... Types>
  constexpr variant_alternative_t<I, variant<Types...>>& get(variant<Types...>& v);
template<size_t I, class... Types>
  constexpr variant_alternative_t<I, variant<Types...>>&& get(variant<Types...>&& v);
template<size_t I, class... Types>
  constexpr const variant_alternative_t<I, variant<Types...>>& get(const variant<Types...>& v);
template<size_t I, class... Types>
  constexpr const variant_alternative_t<I, variant<Types...>>&& get(const variant<Types...>&& v);
```

**3** *Mandates:* `I` &lt; `sizeof...(Types)`.

**4** *Effects:* If `v.index()` is `I`, returns a reference to the object stored in the `variant`. Otherwise, throws an exception of type `bad_variant_access`.

```cpp
template<class T, class... Types> constexpr T& get(variant<Types...>& v);
template<class T, class... Types> constexpr T&& get(variant<Types...>&& v);
template<class T, class... Types> constexpr const T& get(const variant<Types...>& v);
template<class T, class... Types> constexpr const T&& get(const variant<Types...>&& v);
```

**5** *Mandates:* The type `T` occurs exactly once in `Types`.

**6** *Effects:* If `v` holds a value of type `T`, returns a reference to that value. Otherwise, throws an exception of type `bad_variant_access`.

```cpp
template<size_t I, class... Types>
  constexpr add_pointer_t<variant_alternative_t<I, variant<Types...>>>
    get_if(variant<Types...>* v) noexcept;
template<size_t I, class... Types>
  constexpr add_pointer_t<const variant_alternative_t<I, variant<Types...>>>
    get_if(const variant<Types...>* v) noexcept;
```

**7** *Mandates:* `I` &lt; `sizeof...(Types)`.

**8** *Returns:* A pointer to the value stored in the `variant`, if `v != nullptr` and `v->index() == I`. Otherwise, returns `nullptr`.

```cpp
template<class T, class... Types>
  constexpr add_pointer_t<T>
    get_if(variant<Types...>* v) noexcept;
template<class T, class... Types>
  constexpr add_pointer_t<const T>
    get_if(const variant<Types...>* v) noexcept;
```

**9** *Mandates:* The type `T` occurs exactly once in `Types`.

**10** *Effects:* Equivalent to: `return get_if<i>(v);` with *i* being the zero-based index of `T` in `Types`.
