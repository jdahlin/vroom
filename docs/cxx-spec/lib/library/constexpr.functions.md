# 16.5.5.7 Constexpr functions and constructors [constexpr.functions]

[Contents](../../README.md) · up: [16.5.5 Conforming implementations \[conforming\]](conforming.md) · prev: [16.5.5.6 Friend functions \[hidden.friends\]](hidden.friends.md) · next: [16.5.5.8 Requirements for stable algorithms \[algorithm.stable\]](algorithm.stable.md)

**1** This document explicitly requires that certain standard library functions are `constexpr` ([[dcl.constexpr]](../../dcl/dcl.constexpr.md)). An implementation shall not declare any standard library function signature as `constexpr` except for those where it is explicitly required. Within any header that provides any non-defining declarations of constexpr functions or constructors an implementation shall provide corresponding definitions.
