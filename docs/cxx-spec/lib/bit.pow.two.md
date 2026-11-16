# 26.5.4 Integral powers of 2 [bit.pow.two]

[Contents](../README.md) · up: [26.5 Bit manipulation \[bit\]](bit.md) · prev: [26.5.3 Function template `bit_cast` \[bit.cast\]](bit.cast.md) · next: [26.5.5 Rotating \[bit.rotate\]](bit.rotate.md)

```cpp
template<class T>
  constexpr bool has_single_bit(T x) noexcept;
```

**1** *Constraints:* `T` is an unsigned integer type ([[basic.fundamental]](../basic.fundamental.md)).

**2** *Returns:* `true` if `x` is an integral power of two; `false` otherwise.

```cpp
template<class T>
  constexpr T bit_ceil(T x);
```

**3** Let *N* be the smallest power of 2 greater than or equal to `x`.

**4** *Constraints:* `T` is an unsigned integer type ([[basic.fundamental]](../basic.fundamental.md)).

**5** *Preconditions:* *N* is representable as a value of type `T`.

**6** *Returns:* *N*.

**7** *Throws:* Nothing.

**8** *Remarks:* A function call expression that violates the precondition in the *Preconditions:* element is not a core constant expression ([[expr.const]](../expr.const.md)).

```cpp
template<class T>
  constexpr T bit_floor(T x) noexcept;
```

**9** *Constraints:* `T` is an unsigned integer type ([[basic.fundamental]](../basic.fundamental.md)).

**10** *Returns:* If `x == 0`, `0`; otherwise the maximal value `y` such that `has_single_bit(y)` is `true` and `y <= x`.

```cpp
template<class T>
  constexpr T bit_width(T x) noexcept;
```

**11** *Constraints:* `T` is an unsigned integer type ([[basic.fundamental]](../basic.fundamental.md)).

**12** *Returns:* If `x == 0`, `0`; otherwise one plus the base-2 logarithm of `x`, with any fractional part discarded.
