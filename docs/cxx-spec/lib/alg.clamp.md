# 25.8.9 Bounded value [alg.clamp]

[Contents](../README.md) · up: [25.8 Sorting and related operations \[alg.sorting\]](alg.sorting.md) · prev: [25.8.8 Minimum and maximum \[alg.min.max\]](alg.min.max.md) · next: [25.8.10 Lexicographical comparison \[alg.lex.comparison\]](alg.lex.comparison.md)

```cpp
template<class T>
  constexpr const T& clamp(const T& v, const T& lo, const T& hi);
template<class T, class Compare>
  constexpr const T& clamp(const T& v, const T& lo, const T& hi, Compare comp);
template<class T, class Proj = identity,
         indirect_strict_weak_order<projected<const T*, Proj>> Comp = ranges::less>
  constexpr const T&
    ranges::clamp(const T& v, const T& lo, const T& hi, Comp comp = {}, Proj proj = {});
```

**1** Let `comp` be `less{}` for the overloads with no parameter `comp`, and let `proj` be `identity{}` for the overloads with no parameter `proj`.

**2** *Preconditions:* `bool(comp(proj(hi), proj(lo)))` is `false`. For the first form, type `T` meets the *Cpp17LessThanComparable* requirements ([Table 26](utility.arg.requirements.md#tab:cpp17.lessthancomparable)).

**3** *Returns:* `lo` if `bool(comp(proj(v), proj(lo)))` is `true`, `hi` if `bool(comp(proj(hi), proj(v)))` is `true`, otherwise `v`.

**4** [*Note 1*: If NaN is avoided, `T` can be a floating-point type. — *end note*]

**5** *Complexity:* At most two comparisons and three applications of the projection.
