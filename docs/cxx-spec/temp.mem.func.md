# 13.7.1.1 Member functions of class templates [temp.mem.func]

[Contents](README.md) · up: [13.7.1 Class templates \[temp.class\]](temp.class.md) · prev: [13.7.1 Class templates \[temp.class\]](temp.class.md) · next: [13.7.1.2 Deduction guides \[temp.deduct.guide\]](temp.deduct.guide.md)

**1** A member function of a class template may be defined outside of the class template definition in which it is declared. [*Example 1*:

```cpp
template<class T> class Array {
  T* v;
  int sz;
public:
  explicit Array(int);
  T& operator[](int);
  T& elem(int i) { return v[i]; }
};
```

declares three member functions of a class template. The subscript function might be defined like this:

```cpp
template<class T> T& Array<T>::operator[](int i) {
  if (i<0 || sz<=i) error("Array: range error");
  return v[i];
}
```

A constrained member function can be defined out of line:

```cpp
template<typename T> concept C = requires {
  typename T::type;
};

template<typename T> struct S {
  void f() requires C<T>;
  void g() requires C<T>;
};

template<typename T>
  void S<T>::f() requires C<T> { }      // OK
template<typename T>
  void S<T>::g() { }                    // error: no matching function in S<T>
```

— *end example*]

**2** The *template-argument*s for a member function of a class template are determined by the *template-argument*s of the type of the object for which the member function is called. [*Example 2*: The *template-argument* for `Array<T>::operator[]` will be determined by the `Array` to which the subscripting operation is applied.

```cpp
Array<int> v1(20);
Array<dcomplex> v2(30);

v1[3] = 7;                              // Array<int>::operator[]
v2[3] = dcomplex(7,8);                  // Array<dcomplex>::operator[]
```

— *end example*]
