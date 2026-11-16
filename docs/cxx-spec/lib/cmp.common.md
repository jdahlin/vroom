# 17.11.3 Class template `common_comparison_category` [cmp.common]

[Contents](../README.md) · up: [17.11 Comparisons \[cmp\]](cmp.md) · prev: [17.11.2.4 Class `strong_ordering` \[cmp.strongord\]](cmp.strongord.md) · next: [17.11.4 Concept `three_way_comparable` \[cmp.concept\]](cmp.concept.md)

**1** The type `common_comparison_category` provides an alias for the strongest comparison category to which all of the template arguments can be converted. [*Note 1*: A comparison category type is stronger than another if they are distinct types and an instance of the former can be converted to an instance of the latter. — *end note*]

```cpp
template<class... Ts>
struct common_comparison_category {
  using type = see below;
};
```

**2** *Remarks:* The member *typedef-name* `type` denotes the common comparison type ([[class.spaceship]](../class.spaceship.md)) of `Ts...`, the expanded parameter pack, or `void` if any element of `Ts` is not a comparison category type. [*Note 2*: This is `std::strong_ordering` if the expansion is empty. — *end note*]
