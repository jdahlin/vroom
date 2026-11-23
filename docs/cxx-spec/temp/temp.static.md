# 13.7.1.4 Static data members of class templates [temp.static]

[Contents](../README.md) · up: [13.7.1 Class templates \[temp.class\]](temp.class.md) · prev: [13.7.1.3 Member classes of class templates \[temp.mem.class\]](temp.mem.class.md) · next: [13.7.1.5 Enumeration members of class templates \[temp.mem.enum\]](temp.mem.enum.md)

**1** A definition for a static data member or static data member template may be provided in a namespace scope enclosing the definition of the static member’s class template. [*Example 1*:

```cpp
template<class T> class X {
  static T s;
};
template<class T> T X<T>::s = 0;

struct limits {
  template<class T>
    static const T min;                 // declaration
};

template<class T>
  const T limits::min = { };            // definition
```

— *end example*]

**2** An explicit specialization of a static data member declared as an array of unknown bound can have a different bound from its definition, if any. [*Example 2*:

```cpp
template <class T> struct A {
  static int i[];
};
template <class T> int A<T>::i[4];      // 4 elements
template <> int A<int>::i[] = { 1 };    // OK: 1 element
```

— *end example*]
