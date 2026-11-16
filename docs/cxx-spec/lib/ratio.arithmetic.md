# 20.16.4 Arithmetic on `ratio`s [ratio.arithmetic]

[Contents](../README.md) · up: [20.16 Compile-time rational arithmetic \[ratio\]](ratio.md) · prev: [20.16.3 Class template `ratio` \[ratio.ratio\]](ratio.ratio.md) · next: [20.16.5 Comparison of `ratio`s \[ratio.comparison\]](ratio.comparison.md)

**1** Each of the alias templates `ratio_add`, `ratio_subtract`, `ratio_multiply`, and `ratio_divide` denotes the result of an arithmetic computation on two `ratio`s `R1` and `R2`. With `X` and `Y` computed (in the absence of arithmetic overflow) as specified by [Table 58](ratio.arithmetic.md#tab:ratio.arithmetic), each alias denotes a `ratio<U, V>` such that `U` is the same as `ratio<X, Y>::num` and `V` is the same as `ratio<X, Y>::den`.

**2** If it is not possible to represent `U` or `V` with `intmax_t`, the program is ill-formed. Otherwise, an implementation should yield correct values of `U` and `V`. If it is not possible to represent `X` or `Y` with `intmax_t`, the program is ill-formed unless the implementation yields correct values of `U` and `V`.

<a id="tab:ratio.arithmetic"></a>
**Table 58: Expressions used to perform ratio arithmetic** [tab:ratio.arithmetic]

| Type | Value of `X` | Value of `Y` |
| --- | --- | --- |
| `ratio_add<R1, R2>` | `R1::num * R2::den +` | `R1::den * R2::den` |
|  | `R2::num * R1::den` |  |
| `ratio_subtract<R1, R2>` | `R1::num * R2::den -` | `R1::den * R2::den` |
|  | `R2::num * R1::den` |  |
| `ratio_multiply<R1, R2>` | `R1::num * R2::num` | `R1::den * R2::den` |
| `ratio_divide<R1, R2>` | `R1::num * R2::den` | `R1::den * R2::num` |

**3** [*Example 1*:

```cpp
static_assert(ratio_add<ratio<1, 3>, ratio<1, 6>>::num == 1, "1/3+1/6 == 1/2");
static_assert(ratio_add<ratio<1, 3>, ratio<1, 6>>::den == 2, "1/3+1/6 == 1/2");
static_assert(ratio_multiply<ratio<1, 3>, ratio<3, 2>>::num == 1, "1/3*3/2 == 1/2");
static_assert(ratio_multiply<ratio<1, 3>, ratio<3, 2>>::den == 2, "1/3*3/2 == 1/2");

// The following cases may cause the program to be ill-formed under some implementations
static_assert(ratio_add<ratio<1, INT_MAX>, ratio<1, INT_MAX>>::num == 2,
  "1/MAX+1/MAX == 2/MAX");
static_assert(ratio_add<ratio<1, INT_MAX>, ratio<1, INT_MAX>>::den == INT_MAX,
  "1/MAX+1/MAX == 2/MAX");
static_assert(ratio_multiply<ratio<1, INT_MAX>, ratio<INT_MAX, 2>>::num == 1,
  "1/MAX * MAX/2 == 1/2");
static_assert(ratio_multiply<ratio<1, INT_MAX>, ratio<INT_MAX, 2>>::den == 2,
  "1/MAX * MAX/2 == 1/2");
```

— *end example*]
