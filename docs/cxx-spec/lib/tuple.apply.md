# 20.5.5 Calling a function with a `tuple` of arguments [tuple.apply]

[Contents](../README.md) · up: [20.5 Tuples \[tuple\]](tuple.md) · prev: [20.5.4 Tuple creation functions \[tuple.creation\]](tuple.creation.md) · next: [20.5.6 Tuple helper classes \[tuple.helper\]](tuple.helper.md)

```cpp
template<class F, class Tuple>
  constexpr decltype(auto) apply(F&& f, Tuple&& t);
```

**1** *Effects:* Given the exposition-only function:

```cpp
template<class F, class Tuple, size_t... I>
constexpr decltype(auto) apply-impl(F&& f, Tuple&& t, index_sequence<I...>) {
                                                                        // exposition only
  return INVOKE(std::forward<F>(f), std::get<I>(std::forward<Tuple>(t))...);  // see [func.require]
}
```

Equivalent to:

```cpp
return apply-impl(std::forward<F>(f), std::forward<Tuple>(t),
                  make_index_sequence<tuple_size_v<remove_reference_t<Tuple>>>{});
```

```cpp
template<class T, class Tuple>
  constexpr T make_from_tuple(Tuple&& t);
```

**2** *Effects:* Given the exposition-only function:

```cpp
template<class T, class Tuple, size_t... I>
constexpr T make-from-tuple-impl(Tuple&& t, index_sequence<I...>) {     // exposition only
  return T(get<I>(std::forward<Tuple>(t))...);
}
```

Equivalent to:

```cpp
return make-from-tuple-impl<T>(
           forward<Tuple>(t),
           make_index_sequence<tuple_size_v<remove_reference_t<Tuple>>>{});
```

[*Note 1*: The type of `T` must be supplied as an explicit template parameter, as it cannot be deduced from the argument list. — *end note*]
