# 20.7.3.4 Modifiers [variant.mod]

[Contents](../README.md) · up: [20.7.3 Class template `variant` \[variant.variant\]](variant.variant.md) · prev: [20.7.3.3 Assignment \[variant.assign\]](variant.assign.md) · next: [20.7.3.5 Value status \[variant.status\]](variant.status.md)

```cpp
template<class T, class... Args> T& emplace(Args&&... args);
```

**1** *Constraints:* `is_constructible_v<T, Args...>` is `true`, and `T` occurs exactly once in `Types`.

**2** *Effects:* Equivalent to:

```cpp
return emplace<I>(std::forward<Args>(args)...);
```

where *I* is the zero-based index of `T` in `Types`.

```cpp
template<class T, class U, class... Args> T& emplace(initializer_list<U> il, Args&&... args);
```

**3** *Constraints:* `is_constructible_v<T, initializer_list<U>&, Args...>` is `true`, and `T` occurs exactly once in `Types`.

**4** *Effects:* Equivalent to:

```cpp
return emplace<I>(il, std::forward<Args>(args)...);
```

where *I* is the zero-based index of `T` in `Types`.

```cpp
template<size_t I, class... Args>
  variant_alternative_t<I, variant<Types...>>& emplace(Args&&... args);
```

**5** *Mandates:* `I` &lt; `sizeof...(Types)`.

**6** *Constraints:* `is_constructible_v<T_I, Args...>` is `true`.

**7** *Effects:* Destroys the currently contained value if `valueless_by_exception()` is `false`. Then initializes the contained value as if direct-non-list-initializing a value of type `T`<sub>*I*</sub> with the arguments `std::forward<Args>(args)...`.

**8** *Postconditions:* `index()` is `I`.

**9** *Returns:* A reference to the new contained value.

**10** *Throws:* Any exception thrown during the initialization of the contained value.

**11** *Remarks:* If an exception is thrown during the initialization of the contained value, the `variant` might not hold a value.

```cpp
template<size_t I, class U, class... Args>
  variant_alternative_t<I, variant<Types...>>& emplace(initializer_list<U> il, Args&&... args);
```

**12** *Mandates:* `I` &lt; `sizeof...(Types)`.

**13** *Constraints:* `is_constructible_v<T_I, initializer_list<U>&, Args...>` is `true`.

**14** *Effects:* Destroys the currently contained value if `valueless_by_exception()` is `false`. Then initializes the contained value as if direct-non-list-initializing a value of type `T`<sub>*I*</sub> with the arguments `il, std::forward<Args>(args)...`.

**15** *Postconditions:* `index()` is `I`.

**16** *Returns:* A reference to the new contained value.

**17** *Throws:* Any exception thrown during the initialization of the contained value.

**18** *Remarks:* If an exception is thrown during the initialization of the contained value, the `variant` might not hold a value.
