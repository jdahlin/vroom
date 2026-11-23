# 20.4.4 Tuple-like access to pair [pair.astuple]

[Contents](../../README.md) · up: [20.4 Pairs \[pairs\]](pairs.md) · prev: [20.4.3 Specialized algorithms \[pairs.spec\]](pairs.spec.md) · next: [20.4.5 Piecewise construction \[pair.piecewise\]](pair.piecewise.md)

```cpp
template<class T1, class T2>
  struct tuple_size<pair<T1, T2>> : integral_constant<size_t, 2> { };
```

```cpp
template<size_t I, class T1, class T2>
  struct tuple_element<I, pair<T1, T2>> {
    using type = see below ;
  };
```

**1** *Mandates:* `I` &lt; 2.

**2** *Type:* The type `T1` if `I` is 0, otherwise the type `T2`.

```cpp
template<size_t I, class T1, class T2>
  constexpr tuple_element_t<I, pair<T1, T2>>& get(pair<T1, T2>& p) noexcept;
template<size_t I, class T1, class T2>
  constexpr const tuple_element_t<I, pair<T1, T2>>& get(const pair<T1, T2>& p) noexcept;
template<size_t I, class T1, class T2>
  constexpr tuple_element_t<I, pair<T1, T2>>&& get(pair<T1, T2>&& p) noexcept;
template<size_t I, class T1, class T2>
  constexpr const tuple_element_t<I, pair<T1, T2>>&& get(const pair<T1, T2>&& p) noexcept;
```

**3** *Mandates:* `I` &lt; 2.

**4** *Returns:*

- If `I` is 0, returns a reference to `p.first`.
- If `I` is 1, returns a reference to `p.second`.

```cpp
template<class T1, class T2>
  constexpr T1& get(pair<T1, T2>& p) noexcept;
template<class T1, class T2>
  constexpr const T1& get(const pair<T1, T2>& p) noexcept;
template<class T1, class T2>
  constexpr T1&& get(pair<T1, T2>&& p) noexcept;
template<class T1, class T2>
  constexpr const T1&& get(const pair<T1, T2>&& p) noexcept;
```

**5** *Mandates:* `T1` and `T2` are distinct types.

**6** *Returns:* A reference to `p.first`.

```cpp
template<class T2, class T1>
  constexpr T2& get(pair<T1, T2>& p) noexcept;
template<class T2, class T1>
  constexpr const T2& get(const pair<T1, T2>& p) noexcept;
template<class T2, class T1>
  constexpr T2&& get(pair<T1, T2>&& p) noexcept;
template<class T2, class T1>
  constexpr const T2&& get(const pair<T1, T2>&& p) noexcept;
```

**7** *Mandates:* `T1` and `T2` are distinct types.

**8** *Returns:* A reference to `p.second`.
