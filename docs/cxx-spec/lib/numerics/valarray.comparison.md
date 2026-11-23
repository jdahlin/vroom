# 26.7.3.2 Logical operators [valarray.comparison]

[Contents](../../README.md) · up: [26.7.3 `valarray` non-member operations \[valarray.nonmembers\]](valarray.nonmembers.md) · prev: [26.7.3.1 Binary operators \[valarray.binary\]](valarray.binary.md) · next: [26.7.3.3 Transcendentals \[valarray.transcend\]](valarray.transcend.md)

```cpp
template<class T> valarray<bool> operator==(const valarray<T>&, const valarray<T>&);
template<class T> valarray<bool> operator!=(const valarray<T>&, const valarray<T>&);
template<class T> valarray<bool> operator< (const valarray<T>&, const valarray<T>&);
template<class T> valarray<bool> operator> (const valarray<T>&, const valarray<T>&);
template<class T> valarray<bool> operator<=(const valarray<T>&, const valarray<T>&);
template<class T> valarray<bool> operator>=(const valarray<T>&, const valarray<T>&);
template<class T> valarray<bool> operator&&(const valarray<T>&, const valarray<T>&);
template<class T> valarray<bool> operator||(const valarray<T>&, const valarray<T>&);
```

**1** *Mandates:* The indicated operator can be applied to operands of type `T` and returns a value of type `bool` or which can be unambiguously implicitly converted to `bool`.

**2** *Preconditions:* The two array arguments have the same length.

**3** *Returns:* A `valarray<bool>` whose length is equal to the length of the array arguments. Each element of the returned array is initialized with the result of applying the indicated operator to the corresponding elements of the argument arrays.

```cpp
template<class T> valarray<bool> operator==(const valarray<T>&,
                                            const typename valarray<T>::value_type&);
template<class T> valarray<bool> operator==(const typename valarray<T>::value_type&,
                                            const valarray<T>&);
template<class T> valarray<bool> operator!=(const valarray<T>&,
                                            const typename valarray<T>::value_type&);
template<class T> valarray<bool> operator!=(const typename valarray<T>::value_type&,
                                            const valarray<T>&);
template<class T> valarray<bool> operator< (const valarray<T>&,
                                            const typename valarray<T>::value_type&);
template<class T> valarray<bool> operator< (const typename valarray<T>::value_type&,
                                            const valarray<T>&);
template<class T> valarray<bool> operator> (const valarray<T>&,
                                            const typename valarray<T>::value_type&);
template<class T> valarray<bool> operator> (const typename valarray<T>::value_type&,
                                            const valarray<T>&);
template<class T> valarray<bool> operator<=(const valarray<T>&,
                                            const typename valarray<T>::value_type&);
template<class T> valarray<bool> operator<=(const typename valarray<T>::value_type&,
                                            const valarray<T>&);
template<class T> valarray<bool> operator>=(const valarray<T>&,
                                            const typename valarray<T>::value_type&);
template<class T> valarray<bool> operator>=(const typename valarray<T>::value_type&,
                                            const valarray<T>&);
template<class T> valarray<bool> operator&&(const valarray<T>&,
                                            const typename valarray<T>::value_type&);
template<class T> valarray<bool> operator&&(const typename valarray<T>::value_type&,
                                            const valarray<T>&);
template<class T> valarray<bool> operator||(const valarray<T>&,
                                            const typename valarray<T>::value_type&);
template<class T> valarray<bool> operator||(const typename valarray<T>::value_type&,
                                            const valarray<T>&);
```

**4** *Mandates:* The indicated operator can be applied to operands of type `T` and returns a value of type `bool` or which can be unambiguously implicitly converted to `bool`.

**5** *Returns:* A `valarray<bool>` whose length is equal to the length of the array argument. Each element of the returned array is initialized with the result of applying the indicated operator to the corresponding element of the array and the non-array argument.
