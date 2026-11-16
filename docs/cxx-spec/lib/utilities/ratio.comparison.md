# 20.16.5 Comparison of `ratio`s [ratio.comparison]

[Contents](../../README.md) · up: [20.16 Compile-time rational arithmetic \[ratio\]](ratio.md) · prev: [20.16.4 Arithmetic on `ratio`s \[ratio.arithmetic\]](ratio.arithmetic.md) · next: [20.16.6 SI types for `ratio` \[ratio.si\]](ratio.si.md)

```cpp
template<class R1, class R2>
  struct ratio_equal : bool_constant<R1::num == R2::num && R1::den == R2::den> { };
```

```cpp
template<class R1, class R2>
  struct ratio_not_equal : bool_constant<!ratio_equal_v<R1, R2>> { };
```

```cpp
template<class R1, class R2>
  struct ratio_less : bool_constant<see below> { };
```

**1** If `R1::num` × `R2::den` is less than `R2::num` × `R1::den`, `ratio_less<R1, R2>` shall be derived from `bool_constant<true>`; otherwise it shall be derived from `bool_constant<false>`. Implementations may use other algorithms to compute this relationship to avoid overflow. If overflow occurs, the program is ill-formed.

```cpp
template<class R1, class R2>
  struct ratio_less_equal : bool_constant<!ratio_less_v<R2, R1>> { };
```

```cpp
template<class R1, class R2>
  struct ratio_greater : bool_constant<ratio_less_v<R2, R1>> { };
```

```cpp
template<class R1, class R2>
  struct ratio_greater_equal : bool_constant<!ratio_less_v<R1, R2>> { };
```
