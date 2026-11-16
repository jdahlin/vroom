# 26.7.8.2 Assignment [mask.array.assign]

[Contents](../../README.md) · up: [26.7.8 Class template `mask_array` \[template.mask.array\]](template.mask.array.md) · prev: [26.7.8.1 Overview \[template.mask.array.overview\]](template.mask.array.overview.md) · next: [26.7.8.3 Compound assignment \[mask.array.comp.assign\]](mask.array.comp.assign.md)

```cpp
void operator=(const valarray<T>&) const;
const mask_array& operator=(const mask_array&) const;
```

**1** These assignment operators have reference semantics, assigning the values of the argument array elements to selected elements of the `valarray<T>` object to which it refers.
