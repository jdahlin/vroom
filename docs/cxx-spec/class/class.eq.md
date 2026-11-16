# 11.11.2 Equality operator [class.eq]

[Contents](../README.md) · up: [11.11 Comparisons \[class.compare\]](class.compare.md) · prev: [11.11.1 Defaulted comparison operator functions \[class.compare.default\]](class.compare.default.md) · next: [11.11.3 Three-way comparison \[class.spaceship\]](class.spaceship.md)

**1** A defaulted equality operator function ([[over.binary]](../over/over.binary.md)) shall have a declared return type `bool`.

**2** A defaulted `==` operator function for a class `C` is defined as deleted unless, for each `x`<sub>*i*</sub> in the expanded list of subobjects for an object `x` of type `C`, `x`<sub>*i*</sub>`==``x`<sub>*i*</sub> is usable ([[class.compare.default]](class.compare.default.md)).

**3** The return value `V` of a defaulted `==` operator function with parameters `x` and `y` is determined by comparing corresponding elements `x`<sub>*i*</sub> and `y`<sub>*i*</sub> in the expanded lists of subobjects for `x` and `y` (in increasing index order) until the first index *i* where `x`<sub>*i*</sub>`==``y`<sub>*i*</sub> yields a result value which, when contextually converted to `bool`, yields `false`. If no such index exists, `V` is `true`. Otherwise, `V` is `false`.

**4** [*Example 1*:

```cpp
struct D {
  int i;
  friend bool operator==(const D& x, const D& y) = default;
                                                // OK, returns x.i == y.i
};
```

— *end example*]
