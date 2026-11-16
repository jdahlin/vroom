# 26.7.7.3 Compound assignment [gslice.array.comp.assign]

[Contents](../../README.md) · up: [26.7.7 Class template `gslice_array` \[template.gslice.array\]](template.gslice.array.md) · prev: [26.7.7.2 Assignment \[gslice.array.assign\]](gslice.array.assign.md) · next: [26.7.7.4 Fill function \[gslice.array.fill\]](gslice.array.fill.md)

```cpp
void operator*= (const valarray<T>&) const;
void operator/= (const valarray<T>&) const;
void operator%= (const valarray<T>&) const;
void operator+= (const valarray<T>&) const;
void operator-= (const valarray<T>&) const;
void operator^= (const valarray<T>&) const;
void operator&= (const valarray<T>&) const;
void operator|= (const valarray<T>&) const;
void operator<<=(const valarray<T>&) const;
void operator>>=(const valarray<T>&) const;
```

**1** These compound assignments have reference semantics, applying the indicated operation to the elements of the argument array and selected elements of the `valarray<T>` object to which the `gslice_array` object refers.
