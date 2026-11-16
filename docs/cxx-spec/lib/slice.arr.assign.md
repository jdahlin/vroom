# 26.7.5.2 Assignment [slice.arr.assign]

[Contents](../README.md) · up: [26.7.5 Class template `slice_array` \[template.slice.array\]](template.slice.array.md) · prev: [26.7.5.1 Overview \[template.slice.array.overview\]](template.slice.array.overview.md) · next: [26.7.5.3 Compound assignment \[slice.arr.comp.assign\]](slice.arr.comp.assign.md)

```cpp
void operator=(const valarray<T>&) const;
const slice_array& operator=(const slice_array&) const;
```

**1** These assignment operators have reference semantics, assigning the values of the argument array elements to selected elements of the `valarray<T>` object to which the `slice_array` object refers.
