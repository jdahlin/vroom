# 20.4.3 Specialized algorithms [pairs.spec]

[Contents](../README.md) · up: [20.4 Pairs \[pairs\]](pairs.md) · prev: [20.4.2 Class template `pair` \[pairs.pair\]](pairs.pair.md) · next: [20.4.4 Tuple-like access to pair \[pair.astuple\]](pair.astuple.md)

```cpp
template<class T1, class T2>
  constexpr bool operator==(const pair<T1, T2>& x, const pair<T1, T2>& y);
```

**1** *Returns:* `x.first == y.first && x.second == y.second`.

```cpp
template<class T1, class T2>
  constexpr common_comparison_category_t<synth-three-way-result<T1>,
                                         synth-three-way-result<T2>>
    operator<=>(const pair<T1, T2>& x, const pair<T1, T2>& y);
```

**2** *Effects:* Equivalent to:

```cpp
if (auto c = synth-three-way(x.first, y.first); c != 0) return c;
return synth-three-way(x.second, y.second);
```

```cpp
template<class T1, class T2>
  constexpr void swap(pair<T1, T2>& x, pair<T1, T2>& y) noexcept(noexcept(x.swap(y)));
```

**3** *Constraints:* `is_swappable_v<T1>` is `true` and `is_swappable_v<T2>` is `true`.

**4** *Effects:* Equivalent to `x.swap(y)`.

```cpp
template<class T1, class T2>
  constexpr pair<unwrap_ref_decay_t<T1>, unwrap_ref_decay_t<T2>> make_pair(T1&& x, T2&& y);
```

**5** *Returns:*

```cpp
pair<unwrap_ref_decay_t<T1>,
     unwrap_ref_decay_t<T2>>(std::forward<T1>(x), std::forward<T2>(y))
```

**6** [*Example 1*: In place of:

```cpp
return pair<int, double>(5, 3.1415926);     // explicit types
```

a C++ program may contain:

```cpp
return make_pair(5, 3.1415926);             // types are deduced
```

— *end example*]
