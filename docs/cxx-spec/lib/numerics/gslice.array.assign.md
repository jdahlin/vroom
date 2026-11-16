# 26.7.7.2 Assignment [gslice.array.assign]

[Contents](../../README.md) · up: [26.7.7 Class template `gslice_array` \[template.gslice.array\]](template.gslice.array.md) · prev: [26.7.7.1 Overview \[template.gslice.array.overview\]](template.gslice.array.overview.md) · next: [26.7.7.3 Compound assignment \[gslice.array.comp.assign\]](gslice.array.comp.assign.md)

```cpp
void operator=(const valarray<T>&) const;
const gslice_array& operator=(const gslice_array&) const;
```

**1** These assignment operators have reference semantics, assigning the values of the argument array elements to selected elements of the `valarray<T>` object to which the `gslice_array` refers.
