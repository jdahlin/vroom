# 26.7.5.4 Fill function [slice.arr.fill]

[Contents](../README.md) · up: [26.7.5 Class template `slice_array` \[template.slice.array\]](template.slice.array.md) · prev: [26.7.5.3 Compound assignment \[slice.arr.comp.assign\]](slice.arr.comp.assign.md) · next: [26.7.6 The `gslice` class \[class.gslice\]](class.gslice.md)

```cpp
void operator=(const T&) const;
```

**1** This function has reference semantics, assigning the value of its argument to the elements of the `valarray<T>` object to which the `slice_array` object refers.
