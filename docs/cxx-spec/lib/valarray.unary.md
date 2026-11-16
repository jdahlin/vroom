# 26.7.2.6 Unary operators [valarray.unary]

[Contents](../README.md) · up: [26.7.2 Class template `valarray` \[template.valarray\]](template.valarray.md) · prev: [26.7.2.5 Subset operations \[valarray.sub\]](valarray.sub.md) · next: [26.7.2.7 Compound assignment \[valarray.cassign\]](valarray.cassign.md)

```cpp
valarray operator+() const;
valarray operator-() const;
valarray operator~() const;
valarray<bool> operator!() const;
```

**1** *Mandates:* The indicated operator can be applied to operands of type `T` and returns a value of type `T` (`bool` for `operator!`) or which may be unambiguously implicitly converted to type `T` (`bool` for `operator!`).

**2** *Returns:* A `valarray` whose length is `size()`. Each element of the returned array is initialized with the result of applying the indicated operator to the corresponding element of the array.
