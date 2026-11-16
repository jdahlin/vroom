# 18.4.7 Arithmetic concepts [concepts.arithmetic]

[Contents](../README.md) · up: [18.4 Language-related concepts \[concepts.lang\]](concepts.lang.md) · prev: [18.4.6 Concept `common_with` \[concept.common\]](concept.common.md) · next: [18.4.8 Concept `assignable_from` \[concept.assignable\]](concept.assignable.md)

```cpp
template<class T>
  concept integral = is_integral_v<T>;
template<class T>
  concept signed_integral = integral<T> && is_signed_v<T>;
template<class T>
  concept unsigned_integral = integral<T> && !signed_integral<T>;
template<class T>
  concept floating_point = is_floating_point_v<T>;
```

**1** [*Note 1*: `signed_integral` can be modeled even by types that are not signed integer types ([[basic.fundamental]](../basic.fundamental.md)); for example, `char`. — *end note*]

**2** [*Note 2*: `unsigned_integral` can be modeled even by types that are not unsigned integer types ([[basic.fundamental]](../basic.fundamental.md)); for example, `bool`. — *end note*]
