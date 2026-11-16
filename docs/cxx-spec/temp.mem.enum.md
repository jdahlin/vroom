# 13.7.1.5 Enumeration members of class templates [temp.mem.enum]

[Contents](README.md) · up: [13.7.1 Class templates \[temp.class\]](temp.class.md) · prev: [13.7.1.4 Static data members of class templates \[temp.static\]](temp.static.md) · next: [13.7.2 Member templates \[temp.mem\]](temp.mem.md)

**1** An enumeration member of a class template may be defined outside the class template definition. [*Example 1*:

```cpp
template<class T> struct A {
  enum E : T;
};
A<int> a;
template<class T> enum A<T>::E : T { e1, e2 };
A<int>::E e = A<int>::e1;
```

— *end example*]
