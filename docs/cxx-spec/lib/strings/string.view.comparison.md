# 21.4.4 Non-member comparison functions [string.view.comparison]

[Contents](../../README.md) · up: [21.4 String view classes \[string.view\]](string.view.md) · prev: [21.4.3 Deduction guide \[string.view.deduct\]](string.view.deduct.md) · next: [21.4.5 Inserters and extractors \[string.view.io\]](string.view.io.md)

**1** Let `S` be `basic_string_view<charT, traits>`, and `sv` be an instance of `S`. Implementations shall provide sufficient additional overloads marked `constexpr` and `noexcept` so that an object `t` with an implicit conversion to `S` can be compared according to [Table 71](string.view.comparison.md#tab:string.view.comparison.overloads).

<a id="tab:string.view.comparison.overloads"></a>
**Table 71: Additional `basic_string_view` comparison overloads** [tab:string.view.comparison.overloads]

| Expression | Equivalent to |
| --- | --- |
| `t == sv` | `S(t) == sv` |
| `sv == t` | `sv == S(t)` |
| `t != sv` | `S(t) != sv` |
| `sv != t` | `sv != S(t)` |
| `t < sv` | `S(t) < sv` |
| `sv < t` | `sv < S(t)` |
| `t > sv` | `S(t) > sv` |
| `sv > t` | `sv > S(t)` |
| `t <= sv` | `S(t) <= sv` |
| `sv <= t` | `sv <= S(t)` |
| `t >= sv` | `S(t) >= sv` |
| `sv >= t` | `sv >= S(t)` |
| `t <=> sv` | `S(t) <=> sv` |
| `sv <=> t` | `sv <=> S(t)` |

[*Example 1*: A sample conforming implementation for `operator==` would be:

```cpp
template<class charT, class traits>
  constexpr bool operator==(basic_string_view<charT, traits> lhs,
                            basic_string_view<charT, traits> rhs) noexcept {
    return lhs.compare(rhs) == 0;
  }
template<class charT, class traits>
  constexpr bool operator==(basic_string_view<charT, traits> lhs,
                            type_identity_t<basic_string_view<charT, traits>> rhs) noexcept {
    return lhs.compare(rhs) == 0;
  }
```

— *end example*]

```cpp
template<class charT, class traits>
  constexpr bool operator==(basic_string_view<charT, traits> lhs,
                            basic_string_view<charT, traits> rhs) noexcept;
```

**2** *Returns:* `lhs.compare(rhs) == 0`.

```cpp
template<class charT, class traits>
  constexpr see below operator<=>(basic_string_view<charT, traits> lhs,
                                  basic_string_view<charT, traits> rhs) noexcept;
```

**3** Let `R` denote the type `traits::comparison_category` if it exists, otherwise `R` is `weak_ordering`.

**4** *Returns:* `static_cast<R>(lhs.compare(rhs) <=> 0)`.
