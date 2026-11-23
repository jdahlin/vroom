# 26.7.7.4 Fill function [gslice.array.fill]

[Contents](../../README.md) · up: [26.7.7 Class template `gslice_array` \[template.gslice.array\]](template.gslice.array.md) · prev: [26.7.7.3 Compound assignment \[gslice.array.comp.assign\]](gslice.array.comp.assign.md) · next: [26.7.8 Class template `mask_array` \[template.mask.array\]](template.mask.array.md)

```cpp
void operator=(const T&) const;
```

**1** This function has reference semantics, assigning the value of its argument to the elements of the `valarray<T>` object to which the `gslice_array` object refers.
