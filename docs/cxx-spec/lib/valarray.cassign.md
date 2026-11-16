# 26.7.2.7 Compound assignment [valarray.cassign]

[Contents](../README.md) · up: [26.7.2 Class template `valarray` \[template.valarray\]](template.valarray.md) · prev: [26.7.2.6 Unary operators \[valarray.unary\]](valarray.unary.md) · next: [26.7.2.8 Member functions \[valarray.members\]](valarray.members.md)

```cpp
valarray& operator*= (const valarray& v);
valarray& operator/= (const valarray& v);
valarray& operator%= (const valarray& v);
valarray& operator+= (const valarray& v);
valarray& operator-= (const valarray& v);
valarray& operator^= (const valarray& v);
valarray& operator&= (const valarray& v);
valarray& operator|= (const valarray& v);
valarray& operator<<=(const valarray& v);
valarray& operator>>=(const valarray& v);
```

**1** *Mandates:* The indicated operator can be applied to two operands of type `T`.

**2** *Preconditions:* `size() == v.size()` is `true`.

The value of an element in the left-hand side of a valarray compound assignment operator does not depend on the value of another element in that left hand side.

**3** *Effects:* Each of these operators performs the indicated operation on each of the elements of `*this` and the corresponding element of `v`.

**4** *Returns:* `*this`.

**5** *Remarks:* The appearance of an array on the left-hand side of a compound assignment does not invalidate references or pointers.

```cpp
valarray& operator*= (const T& v);
valarray& operator/= (const T& v);
valarray& operator%= (const T& v);
valarray& operator+= (const T& v);
valarray& operator-= (const T& v);
valarray& operator^= (const T& v);
valarray& operator&= (const T& v);
valarray& operator|= (const T& v);
valarray& operator<<=(const T& v);
valarray& operator>>=(const T& v);
```

**6** *Mandates:* The indicated operator can be applied to two operands of type `T`.

**7** *Effects:* Each of these operators applies the indicated operation to each element of `*this` and `v`.

**8** *Returns:* `*this`

**9** *Remarks:* The appearance of an array on the left-hand side of a compound assignment does not invalidate references or pointers to the elements of the array.
