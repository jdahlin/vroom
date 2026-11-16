# 13.7.5.3 Members of class template specializations [temp.class.spec.mfunc]

[Contents](../README.md) · up: [13.7.5 Class template partial specializations \[temp.class.spec\]](temp.class.spec.md) · prev: [13.7.5.2 Partial ordering of class template specializations \[temp.class.order\]](temp.class.order.md) · next: [13.7.6 Function templates \[temp.fct\]](temp.fct.md)

**1** The template parameter list of a member of a class template partial specialization shall match the template parameter list of the class template partial specialization. The template argument list of a member of a class template partial specialization shall match the template argument list of the class template partial specialization. A class template partial specialization is a distinct template. The members of the class template partial specialization are unrelated to the members of the primary template. Class template partial specialization members that are used in a way that requires a definition shall be defined; the definitions of members of the primary template are never used as definitions for members of a class template partial specialization. An explicit specialization of a member of a class template partial specialization is declared in the same way as an explicit specialization of the primary template. [*Example 1*:

```cpp
// primary class template
template<class T, int I> struct A {
  void f();
};

// member of primary class template
template<class T, int I> void A<T,I>::f() { }

// class template partial specialization
template<class T> struct A<T,2> {
  void f();
  void g();
  void h();
};

// member of class template partial specialization
template<class T> void A<T,2>::g() { }

// explicit specialization
template<> void A<char,2>::h() { }

int main() {
  A<char,0> a0;
  A<char,2> a2;
  a0.f();           // OK, uses definition of primary template's member
  a2.g();           // OK, uses definition of partial specialization's member
  a2.h();           // OK, uses definition of explicit specialization's member
  a2.f();           // error: no definition of f for A<T,2>; the primary template is not used here
}
```

— *end example*]

**2** If a member template of a class template is partially specialized, the member template partial specializations are member templates of the enclosing class template; if the enclosing class template is instantiated ([[temp.inst]](temp.inst.md), [[temp.explicit]](temp.explicit.md)), a declaration for every member template partial specialization is also instantiated as part of creating the members of the class template specialization. If the primary member template is explicitly specialized for a given (implicit) specialization of the enclosing class template, the partial specializations of the member template are ignored for this specialization of the enclosing class template. If a partial specialization of the member template is explicitly specialized for a given (implicit) specialization of the enclosing class template, the primary member template and its other partial specializations are still considered for this specialization of the enclosing class template. [*Example 2*:

```cpp
template<class T> struct A {
  template<class T2> struct B {};                               // #1
  template<class T2> struct B<T2*> {};                          // #2
};

template<> template<class T2> struct A<short>::B {};            // #3

A<char>::B<int*>  abcip;                                        // uses #2
A<short>::B<int*> absip;                                        // uses #3
A<char>::B<int>  abci;                                          // uses #1
```

— *end example*]
