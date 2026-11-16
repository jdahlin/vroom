# 13.7.1.3 Member classes of class templates [temp.mem.class]

[Contents](README.md) · up: [13.7.1 Class templates \[temp.class\]](temp.class.md) · prev: [13.7.1.2 Deduction guides \[temp.deduct.guide\]](temp.deduct.guide.md) · next: [13.7.1.4 Static data members of class templates \[temp.static\]](temp.static.md)

**1** A member class of a class template may be defined outside the class template definition in which it is declared. [*Note 1*: The member class must be defined before its first use that requires an instantiation ([[temp.inst]](temp.inst.md)). For example,

```cpp
template<class T> struct A {
  class B;
};
A<int>::B* b1;                          // OK: requires A to be defined but not A::B
template<class T> class A<T>::B { };
A<int>::B  b2;                          // OK: requires A::B to be defined
```

— *end note*]
