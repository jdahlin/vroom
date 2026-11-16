# 25.10.15 Midpoint [numeric.ops.midpoint]

[Contents](../../README.md) · up: [25.10 Generalized numeric operations \[numeric.ops\]](numeric.ops.md) · prev: [25.10.14 Least common multiple \[numeric.ops.lcm\]](numeric.ops.lcm.md) · next: [25.11 Specialized `<memory>` algorithms \[specialized.algorithms\]](specialized.algorithms.md)

```cpp
template<class T>
  constexpr T midpoint(T a, T b) noexcept;
```

**1** *Constraints:* `T` is an arithmetic type other than `bool`.

**2** *Returns:* Half the sum of `a` and `b`. If `T` is an integer type and the sum is odd, the result is rounded towards `a`.

**3** *Remarks:* No overflow occurs. If `T` is a floating-point type, at most one inexact operation occurs.

```cpp
template<class T>
  constexpr T* midpoint(T* a, T* b);
```

**4** *Constraints:* `T` is an object type.

**5** *Mandates:* `T` is a complete type.

**6** *Preconditions:* `a` and `b` point to, respectively, elements *i* and *j* of the same array object `x`. [*Note 1*: As specified in [[basic.compound]](../../basic/basic.compound.md), an object that is not an array element is considered to belong to a single-element array for this purpose and a pointer past the last element of an array of *n* elements is considered to be equivalent to a pointer to a hypothetical array element *n* for this purpose. — *end note*]

**7** *Returns:* A pointer to array element $i+\frac{j-i}{2}$ of `x`, where the result of the division is truncated towards zero.
