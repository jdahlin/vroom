# 26.7.9.4 Fill function [indirect.array.fill]

[Contents](../../README.md) · up: [26.7.9 Class template `indirect_array` \[template.indirect.array\]](template.indirect.array.md) · prev: [26.7.9.3 Compound assignment \[indirect.array.comp.assign\]](indirect.array.comp.assign.md) · next: [26.7.10 `valarray` range access \[valarray.range\]](valarray.range.md)

```cpp
void operator=(const T&) const;
```

**1** This function has reference semantics, assigning the value of its argument to the elements of the `valarray<T>` object to which the `indirect_array` object refers.
