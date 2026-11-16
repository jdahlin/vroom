# 26.7.3.3 Transcendentals [valarray.transcend]

[Contents](../README.md) · up: [26.7.3 `valarray` non-member operations \[valarray.nonmembers\]](valarray.nonmembers.md) · prev: [26.7.3.2 Logical operators \[valarray.comparison\]](valarray.comparison.md) · next: [26.7.3.4 Specialized algorithms \[valarray.special\]](valarray.special.md)

```cpp
template<class T> valarray<T> abs  (const valarray<T>&);
template<class T> valarray<T> acos (const valarray<T>&);
template<class T> valarray<T> asin (const valarray<T>&);
template<class T> valarray<T> atan (const valarray<T>&);
template<class T> valarray<T> atan2(const valarray<T>&, const valarray<T>&);
template<class T> valarray<T> atan2(const valarray<T>&, const typename valarray<T>::value_type&);
template<class T> valarray<T> atan2(const typename valarray<T>::value_type&, const valarray<T>&);
template<class T> valarray<T> cos  (const valarray<T>&);
template<class T> valarray<T> cosh (const valarray<T>&);
template<class T> valarray<T> exp  (const valarray<T>&);
template<class T> valarray<T> log  (const valarray<T>&);
template<class T> valarray<T> log10(const valarray<T>&);
template<class T> valarray<T> pow  (const valarray<T>&, const valarray<T>&);
template<class T> valarray<T> pow  (const valarray<T>&, const typename valarray<T>::value_type&);
template<class T> valarray<T> pow  (const typename valarray<T>::value_type&, const valarray<T>&);
template<class T> valarray<T> sin  (const valarray<T>&);
template<class T> valarray<T> sinh (const valarray<T>&);
template<class T> valarray<T> sqrt (const valarray<T>&);
template<class T> valarray<T> tan  (const valarray<T>&);
template<class T> valarray<T> tanh (const valarray<T>&);
```

**1** *Mandates:* A unique function with the indicated name can be applied (unqualified) to an operand of type `T`. This function returns a value of type `T` or which can be unambiguously implicitly converted to type `T`.
