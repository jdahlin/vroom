# 26.7.9.3 Compound assignment [indirect.array.comp.assign]

[Contents](../../README.md) · up: [26.7.9 Class template `indirect_array` \[template.indirect.array\]](template.indirect.array.md) · prev: [26.7.9.2 Assignment \[indirect.array.assign\]](indirect.array.assign.md) · next: [26.7.9.4 Fill function \[indirect.array.fill\]](indirect.array.fill.md)

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

**1** These compound assignments have reference semantics, applying the indicated operation to the elements of the argument array and selected elements of the `valarray<T>` object to which the `indirect_array` object refers.

**2** If the `indirect_array` specifies an element in the `valarray<T>` object to which it refers more than once, the behavior is undefined.
