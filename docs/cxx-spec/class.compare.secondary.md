# 11.11.4 Secondary comparison operators [class.compare.secondary]

[Contents](README.md) · up: [11.11 Comparisons \[class.compare\]](class.compare.md) · prev: [11.11.3 Three-way comparison \[class.spaceship\]](class.spaceship.md) · next: [11.12 Free store \[class.free\]](class.free.md)

**1** A *secondary comparison operator* is a relational operator ([[expr.rel]](expr.rel.md)) or the `!=` operator. A defaulted operator function ([[over.binary]](over.binary.md)) for a secondary comparison operator `@` shall have a declared return type `bool`.

**2** The operator function with parameters `x` and `y` is defined as deleted if

- overload resolution ([[over.match]](over.match.md)), as applied to `x @ y`, does not result in a usable candidate, or
- the candidate selected by overload resolution is not a rewritten candidate.

Otherwise, the operator function yields `x @ y`. The defaulted operator function is not considered as a candidate in the overload resolution for the `@` operator.

**3** [*Example 1*:

```cpp
struct HasNoLessThan { };

struct C {
  friend HasNoLessThan operator<=>(const C&, const C&);
  bool operator<(const C&) const = default;             // OK, function is deleted
};
```

— *end example*]
