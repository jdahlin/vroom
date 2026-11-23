# 24.7.1 Range adaptor objects [range.adaptor.object]

[Contents](../../README.md) · up: [24.7 Range adaptors \[range.adaptors\]](range.adaptors.md) · prev: [24.7 Range adaptors \[range.adaptors\]](range.adaptors.md) · next: [24.7.2 Semiregular wrapper \[range.semi.wrap\]](range.semi.wrap.md)

**1** A *range adaptor closure object* is a unary function object that accepts a `viewable_range` argument and returns a `view`. For a range adaptor closure object `C` and an expression `R` such that `decltype((R))` models `viewable_range`, the following expressions are equivalent and yield a `view`:

```cpp
C(R)
R | C
```

Given an additional range adaptor closure object `D`, the expression `C | D` is well-formed and produces another range adaptor closure object such that the following two expressions are equivalent:

```cpp
R | C | D
R | (C | D)
```

**2** A *range adaptor object* is a customization point object ([[customization.point.object]](../library/customization.point.object.md)) that accepts a `viewable_range` as its first argument and returns a `view`.

**3** If a range adaptor object accepts only one argument, then it is a range adaptor closure object.

**4** If a range adaptor object accepts more than one argument, then the following expressions are equivalent:

```cpp
adaptor(range, args...)
adaptor(args...)(range)
range | adaptor(args...)
```

In this case, `adaptor(args...)` is a range adaptor closure object.
