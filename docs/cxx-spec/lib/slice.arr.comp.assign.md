# 26.7.5.3 Compound assignment [slice.arr.comp.assign]

[Contents](../README.md) · up: [26.7.5 Class template `slice_array` \[template.slice.array\]](template.slice.array.md) · prev: [26.7.5.2 Assignment \[slice.arr.assign\]](slice.arr.assign.md) · next: [26.7.5.4 Fill function \[slice.arr.fill\]](slice.arr.fill.md)

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

**1** These compound assignments have reference semantics, applying the indicated operation to the elements of the argument array and selected elements of the `valarray<T>` object to which the `slice_array` object refers.
