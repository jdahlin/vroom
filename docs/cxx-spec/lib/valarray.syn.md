# 26.7.1 Header `<valarray>` synopsis [valarray.syn]

[Contents](../README.md) · up: [26.7 Numeric arrays \[numarray\]](numarray.md) · prev: [26.7 Numeric arrays \[numarray\]](numarray.md) · next: [26.7.2 Class template `valarray` \[template.valarray\]](template.valarray.md)

```cpp
#include <initializer_list>

namespace std {
  template<class T> class valarray;         // An array of type T
  class slice;                              // a BLAS-like slice out of an array
  template<class T> class slice_array;
  class gslice;                             // a generalized slice out of an array
  template<class T> class gslice_array;
  template<class T> class mask_array;       // a masked array
  template<class T> class indirect_array;   // an indirected array

  template<class T> void swap(valarray<T>&, valarray<T>&) noexcept;

  template<class T> valarray<T> operator* (const valarray<T>&, const valarray<T>&);
  template<class T> valarray<T> operator* (const valarray<T>&,
                                           const typename valarray<T>::value_type&);
  template<class T> valarray<T> operator* (const typename valarray<T>::value_type&,
                                           const valarray<T>&);

  template<class T> valarray<T> operator/ (const valarray<T>&, const valarray<T>&);
  template<class T> valarray<T> operator/ (const valarray<T>&,
                                           const typename valarray<T>::value_type&);
  template<class T> valarray<T> operator/ (const typename valarray<T>::value_type&,
                                           const valarray<T>&);

  template<class T> valarray<T> operator% (const valarray<T>&, const valarray<T>&);
  template<class T> valarray<T> operator% (const valarray<T>&,
                                           const typename valarray<T>::value_type&);
  template<class T> valarray<T> operator% (const typename valarray<T>::value_type&,
                                           const valarray<T>&);

  template<class T> valarray<T> operator+ (const valarray<T>&, const valarray<T>&);
  template<class T> valarray<T> operator+ (const valarray<T>&,
                                           const typename valarray<T>::value_type&);
  template<class T> valarray<T> operator+ (const typename valarray<T>::value_type&,
                                           const valarray<T>&);

  template<class T> valarray<T> operator- (const valarray<T>&, const valarray<T>&);
  template<class T> valarray<T> operator- (const valarray<T>&,
                                           const typename valarray<T>::value_type&);
  template<class T> valarray<T> operator- (const typename valarray<T>::value_type&,
                                           const valarray<T>&);

  template<class T> valarray<T> operator^ (const valarray<T>&, const valarray<T>&);
  template<class T> valarray<T> operator^ (const valarray<T>&,
                                           const typename valarray<T>::value_type&);
  template<class T> valarray<T> operator^ (const typename valarray<T>::value_type&,
                                           const valarray<T>&);

  template<class T> valarray<T> operator& (const valarray<T>&, const valarray<T>&);
  template<class T> valarray<T> operator& (const valarray<T>&,
                                           const typename valarray<T>::value_type&);
  template<class T> valarray<T> operator& (const typename valarray<T>::value_type&,
                                           const valarray<T>&);

  template<class T> valarray<T> operator| (const valarray<T>&, const valarray<T>&);
  template<class T> valarray<T> operator| (const valarray<T>&,
                                           const typename valarray<T>::value_type&);
  template<class T> valarray<T> operator| (const typename valarray<T>::value_type&,
                                           const valarray<T>&);

  template<class T> valarray<T> operator<<(const valarray<T>&, const valarray<T>&);
  template<class T> valarray<T> operator<<(const valarray<T>&,
                                           const typename valarray<T>::value_type&);
  template<class T> valarray<T> operator<<(const typename valarray<T>::value_type&,
                                           const valarray<T>&);

  template<class T> valarray<T> operator>>(const valarray<T>&, const valarray<T>&);
  template<class T> valarray<T> operator>>(const valarray<T>&,
                                           const typename valarray<T>::value_type&);
  template<class T> valarray<T> operator>>(const typename valarray<T>::value_type&,
                                           const valarray<T>&);

  template<class T> valarray<bool> operator&&(const valarray<T>&, const valarray<T>&);
  template<class T> valarray<bool> operator&&(const valarray<T>&,
                                              const typename valarray<T>::value_type&);
  template<class T> valarray<bool> operator&&(const typename valarray<T>::value_type&,
                                              const valarray<T>&);

  template<class T> valarray<bool> operator||(const valarray<T>&, const valarray<T>&);
  template<class T> valarray<bool> operator||(const valarray<T>&,
                                              const typename valarray<T>::value_type&);
  template<class T> valarray<bool> operator||(const typename valarray<T>::value_type&,
                                              const valarray<T>&);

  template<class T> valarray<bool> operator==(const valarray<T>&, const valarray<T>&);
  template<class T> valarray<bool> operator==(const valarray<T>&,
                                              const typename valarray<T>::value_type&);
  template<class T> valarray<bool> operator==(const typename valarray<T>::value_type&,
                                              const valarray<T>&);
  template<class T> valarray<bool> operator!=(const valarray<T>&, const valarray<T>&);
  template<class T> valarray<bool> operator!=(const valarray<T>&,
                                              const typename valarray<T>::value_type&);
  template<class T> valarray<bool> operator!=(const typename valarray<T>::value_type&,
                                              const valarray<T>&);

  template<class T> valarray<bool> operator< (const valarray<T>&, const valarray<T>&);
  template<class T> valarray<bool> operator< (const valarray<T>&,
                                              const typename valarray<T>::value_type&);
  template<class T> valarray<bool> operator< (const typename valarray<T>::value_type&,
                                              const valarray<T>&);
  template<class T> valarray<bool> operator> (const valarray<T>&, const valarray<T>&);
  template<class T> valarray<bool> operator> (const valarray<T>&,
                                              const typename valarray<T>::value_type&);
  template<class T> valarray<bool> operator> (const typename valarray<T>::value_type&,
                                              const valarray<T>&);
  template<class T> valarray<bool> operator<=(const valarray<T>&, const valarray<T>&);
  template<class T> valarray<bool> operator<=(const valarray<T>&,
                                              const typename valarray<T>::value_type&);
  template<class T> valarray<bool> operator<=(const typename valarray<T>::value_type&,
                                              const valarray<T>&);
  template<class T> valarray<bool> operator>=(const valarray<T>&, const valarray<T>&);
  template<class T> valarray<bool> operator>=(const valarray<T>&,
                                              const typename valarray<T>::value_type&);
  template<class T> valarray<bool> operator>=(const typename valarray<T>::value_type&,
                                              const valarray<T>&);

  template<class T> valarray<T> abs  (const valarray<T>&);
  template<class T> valarray<T> acos (const valarray<T>&);
  template<class T> valarray<T> asin (const valarray<T>&);
  template<class T> valarray<T> atan (const valarray<T>&);

  template<class T> valarray<T> atan2(const valarray<T>&, const valarray<T>&);
  template<class T> valarray<T> atan2(const valarray<T>&,
                                      const typename valarray<T>::value_type&);
  template<class T> valarray<T> atan2(const typename valarray<T>::value_type&,
                                      const valarray<T>&);

  template<class T> valarray<T> cos  (const valarray<T>&);
  template<class T> valarray<T> cosh (const valarray<T>&);
  template<class T> valarray<T> exp  (const valarray<T>&);
  template<class T> valarray<T> log  (const valarray<T>&);
  template<class T> valarray<T> log10(const valarray<T>&);

  template<class T> valarray<T> pow(const valarray<T>&, const valarray<T>&);
  template<class T> valarray<T> pow(const valarray<T>&, const typename valarray<T>::value_type&);
  template<class T> valarray<T> pow(const typename valarray<T>::value_type&, const valarray<T>&);

  template<class T> valarray<T> sin  (const valarray<T>&);
  template<class T> valarray<T> sinh (const valarray<T>&);
  template<class T> valarray<T> sqrt (const valarray<T>&);
  template<class T> valarray<T> tan  (const valarray<T>&);
  template<class T> valarray<T> tanh (const valarray<T>&);

  template<class T> unspecified1 begin(valarray<T>& v);
  template<class T> unspecified2 begin(const valarray<T>& v);
  template<class T> unspecified1 end(valarray<T>& v);
  template<class T> unspecified2 end(const valarray<T>& v);
}
```

**1** The header `<valarray>` defines five class templates (`valarray`, `slice_array`, `gslice_array`, `mask_array`, and `indirect_array`), two classes (`slice` and `gslice`), and a series of related function templates for representing and manipulating arrays of values.

**2** The `valarray` array classes are defined to be free of certain forms of aliasing, thus allowing operations on these classes to be optimized.

**3** Any function returning a `valarray<T>` is permitted to return an object of another type, provided all the const member functions of `valarray<T>` are also applicable to this type. This return type shall not add more than two levels of template nesting over the most deeply nested argument type.[^1]

**4** Implementations introducing such replacement types shall provide additional functions and operators as follows:

- for every function taking a `const valarray<T>&` other than `begin` and `end` ([[valarray.range]](valarray.range.md)), identical functions taking the replacement types shall be added;
- for every function taking two `const valarray<T>&` arguments, identical functions taking every combination of `const valarray<T>&` and replacement types shall be added.

**5** In particular, an implementation shall allow a `valarray<T>` to be constructed from such replacement types and shall allow assignments and compound assignments of such types to `valarray<T>`, `slice_array<T>`, `gslice_array<T>`, `mask_array<T>` and `indirect_array<T>` objects.

**6** These library functions are permitted to throw a `bad_alloc` ([[bad.alloc]](bad.alloc.md)) exception if there are not sufficient resources available to carry out the operation. Note that the exception is not mandated.

[^1]: [[implimits]](../implimits.md) recommends a minimum number of recursively nested template instantiations. This requirement thus indirectly suggests a minimum allowable complexity for valarray expressions.
