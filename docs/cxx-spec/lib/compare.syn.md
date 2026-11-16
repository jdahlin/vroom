# 17.11.1 Header `<compare>` synopsis [compare.syn]

[Contents](../README.md) · up: [17.11 Comparisons \[cmp\]](cmp.md) · prev: [17.11 Comparisons \[cmp\]](cmp.md) · next: [17.11.2 Comparison category types \[cmp.categories\]](cmp.categories.md)

**1** The header `<compare>` specifies types, objects, and functions for use primarily in connection with the three-way comparison operator ([[expr.spaceship]](../expr.spaceship.md)).

```cpp
namespace std {
  // [cmp.categories], comparison category types
  class partial_ordering;
  class weak_ordering;
  class strong_ordering;

  // named comparison functions
  constexpr bool is_eq  (partial_ordering cmp) noexcept { return cmp == 0; }
  constexpr bool is_neq (partial_ordering cmp) noexcept { return cmp != 0; }
  constexpr bool is_lt  (partial_ordering cmp) noexcept { return cmp < 0; }
  constexpr bool is_lteq(partial_ordering cmp) noexcept { return cmp <= 0; }
  constexpr bool is_gt  (partial_ordering cmp) noexcept { return cmp > 0; }
  constexpr bool is_gteq(partial_ordering cmp) noexcept { return cmp >= 0; }

  // [cmp.common], common comparison category type
  template<class... Ts>
  struct common_comparison_category {
    using type = see below;
  };
  template<class... Ts>
    using common_comparison_category_t = typename common_comparison_category<Ts...>::type;

  // [cmp.concept], concept three_way_comparable
  template<class T, class Cat = partial_ordering>
    concept three_way_comparable = see below;
  template<class T, class U, class Cat = partial_ordering>
    concept three_way_comparable_with = see below;

  // [cmp.result], result of three-way comparison
  template<class T, class U = T> struct compare_three_way_result;

  template<class T, class U = T>
    using compare_three_way_result_t = typename compare_three_way_result<T, U>::type;

  // [comparisons.three.way], class compare_three_way
  struct compare_three_way;

  // [cmp.alg], comparison algorithms
  inline namespace unspecified {
    inline constexpr unspecified strong_order = unspecified;
    inline constexpr unspecified weak_order = unspecified;
    inline constexpr unspecified partial_order = unspecified;
    inline constexpr unspecified compare_strong_order_fallback = unspecified;
    inline constexpr unspecified compare_weak_order_fallback = unspecified;
    inline constexpr unspecified compare_partial_order_fallback = unspecified;
  }
}
```
