# D.4 Array comparisons [depr.array.comp]

[Contents](../README.md) · up: [Annex D Compatibility features \[depr\]](depr.md) · prev: [D.3 Comma operator in subscript expressions \[depr.comma.subscript\]](depr.comma.subscript.md) · next: [D.5 Deprecated `volatile` types \[depr.volatile.type\]](depr.volatile.type.md)

**1** Equality and relational comparisons ([[expr.eq]](../expr/expr.eq.md), [[expr.rel]](../expr/expr.rel.md)) between two operands of array type are deprecated. [*Note 1*: Three-way comparisons ([[expr.spaceship]](../expr/expr.spaceship.md)) between such operands are ill-formed. — *end note*] [*Example 1*:

```cpp
int arr1[5];
int arr2[5];
bool same = arr1 == arr2;       // deprecated, same as &arr1[0] == &arr2[0],
                                // does not compare array contents
auto cmp = arr1 <=> arr2;       // error
```

— *end example*]
