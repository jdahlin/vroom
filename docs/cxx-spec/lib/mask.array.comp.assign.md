# 26.7.8.3 Compound assignment [mask.array.comp.assign]

[Contents](../README.md) · up: [26.7.8 Class template `mask_array` \[template.mask.array\]](template.mask.array.md) · prev: [26.7.8.2 Assignment \[mask.array.assign\]](mask.array.assign.md) · next: [26.7.8.4 Fill function \[mask.array.fill\]](mask.array.fill.md)

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

**1** These compound assignments have reference semantics, applying the indicated operation to the elements of the argument array and selected elements of the `valarray<T>` object to which the mask object refers.
