# 27.4.2 `duration_values` [time.traits.duration.values]

[Contents](../../README.md) · up: [27.4 Time-related traits \[time.traits\]](time.traits.md) · prev: [27.4.1 `treat_as_floating_point` \[time.traits.is.fp\]](time.traits.is.fp.md) · next: [27.4.3 Specializations of `common_type` \[time.traits.specializations\]](time.traits.specializations.md)

```cpp
template<class Rep>
  struct duration_values {
  public:
    static constexpr Rep zero() noexcept;
    static constexpr Rep min() noexcept;
    static constexpr Rep max() noexcept;
};
```

**1** The `duration` template uses the `duration_values` trait to construct special values of the duration’s representation (`Rep`). This is done because the representation might be a class type with behavior which requires some other implementation to return these special values. In that case, the author of that class type should specialize `duration_values` to return the indicated values.

```cpp
static constexpr Rep zero() noexcept;
```

**2** *Returns:* `Rep(0)`. [*Note 1*: `Rep(0)` is specified instead of `Rep()` because `Rep()` may have some other meaning, such as an uninitialized value. — *end note*]

**3** *Remarks:* The value returned shall be the additive identity.

```cpp
static constexpr Rep min() noexcept;
```

**4** *Returns:* `numeric_limits<Rep>::lowest()`.

**5** *Remarks:* The value returned shall compare less than or equal to `zero()`.

```cpp
static constexpr Rep max() noexcept;
```

**6** *Returns:* `numeric_limits<Rep>::max()`.

**7** *Remarks:* The value returned shall compare greater than `zero()`.
