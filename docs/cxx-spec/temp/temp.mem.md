# 13.7.2 Member templates [temp.mem]

[Contents](../README.md) · up: [13.7 Template declarations \[temp.decls\]](temp.decls.md) · prev: [13.7.1.5 Enumeration members of class templates \[temp.mem.enum\]](temp.mem.enum.md) · next: [13.7.3 Variadic templates \[temp.variadic\]](temp.variadic.md)

**1** A template can be declared within a class or class template; such a template is called a member template. A member template can be defined within or outside its class definition or class template definition. A member template of a class template that is defined outside of its class template definition shall be specified with a *template-head* equivalent to that of the class template followed by a *template-head* equivalent to that of the member template ([[temp.over.link]](temp.over.link.md)). [*Example 1*:

```cpp
template<class T> struct string {
  template<class T2> int compare(const T2&);
  template<class T2> string(const string<T2>& s) { /* ... */ }
};

template<class T> template<class T2> int string<T>::compare(const T2& s) {
}
```

— *end example*] [*Example 2*:

```cpp
template<typename T> concept C1 = true;
template<typename T> concept C2 = sizeof(T) <= 4;

template<C1 T> struct S {
  template<C2 U> void f(U);
  template<C2 U> void g(U);
};

template<C1 T> template<C2 U>
void S<T>::f(U) { }             // OK
template<C1 T> template<typename U>
void S<T>::g(U) { }             // error: no matching function in S<T>
```

— *end example*]

**2** A local class of non-closure type shall not have member templates. Access control rules ([[class.access]](../class/class.access.md)) apply to member template names. A destructor shall not be a member template. A non-template member function ([[dcl.fct]](../dcl/dcl.fct.md)) with a given name and type and a member function template of the same name, which could be used to generate a specialization of the same type, can both be declared in a class. When both exist, a use of that name and type refers to the non-template member unless an explicit template argument list is supplied. [*Example 3*:

```cpp
template <class T> struct A {
  void f(int);
  template <class T2> void f(T2);
};

template <> void A<int>::f(int) { }                 // non-template member function
template <> template <> void A<int>::f<>(int) { }   // member function template specialization

int main() {
  A<char> ac;
  ac.f(1);                                          // non-template
  ac.f('c');                                        // template
  ac.f<>(1);                                        // template
}
```

— *end example*]

**3** A member function template shall not be virtual. [*Example 4*:

```cpp
template <class T> struct AA {
  template <class C> virtual void g(C);             // error
  virtual void f();                                 // OK
};
```

— *end example*]

**4** A specialization of a member function template does not override a virtual function from a base class. [*Example 5*:

```cpp
class B {
  virtual void f(int);
};

class D : public B {
  template <class T> void f(T); // does not override B::f(int)
  void f(int i) { f<>(i); }     // overriding function that calls the template instantiation
};
```

— *end example*]

**5** A specialization of a conversion function template is referenced in the same way as a non-template conversion function that converts to the same type. [*Example 6*:

```cpp
struct A {
  template <class T> operator T*();
};
template <class T> A::operator T*(){ return 0; }
template <> A::operator char*(){ return 0; }        // specialization
template A::operator void*();                       // explicit instantiation

int main() {
  A a;
  int* ip;
  ip = a.operator int*();       // explicit call to template operator A::operator int*()
}
```

— *end example*] [*Note 1*: There is no syntax to form a *template-id* ([[temp.names]](temp.names.md)) by providing an explicit template argument list ([[temp.arg.explicit]](temp.arg.explicit.md)) for a conversion function template ([[class.conv.fct]](../class/class.conv.fct.md)). — *end note*]

**6** A specialization of a conversion function template is not found by name lookup. Instead, any conversion function templates visible in the context of the use are considered. For each such operator, if argument deduction succeeds ([[temp.deduct.conv]](temp.deduct.conv.md)), the resulting specialization is used as if found by name lookup.

**7** A *using-declaration* in a derived class cannot refer to a specialization of a conversion function template in a base class.

**8** Overload resolution ([[over.ics.rank]](../over/over.ics.rank.md)) and partial ordering ([[temp.func.order]](temp.func.order.md)) are used to select the best conversion function among multiple specializations of conversion function templates and/or non-template conversion functions.
