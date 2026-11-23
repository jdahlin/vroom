# 26.7.8.4 Fill function [mask.array.fill]

[Contents](../../README.md) · up: [26.7.8 Class template `mask_array` \[template.mask.array\]](template.mask.array.md) · prev: [26.7.8.3 Compound assignment \[mask.array.comp.assign\]](mask.array.comp.assign.md) · next: [26.7.9 Class template `indirect_array` \[template.indirect.array\]](template.indirect.array.md)

```cpp
void operator=(const T&) const;
```

**1** This function has reference semantics, assigning the value of its argument to the elements of the `valarray<T>` object to which the `mask_array` object refers.
