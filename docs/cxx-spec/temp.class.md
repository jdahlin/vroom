# 13.7.1 Class templates [temp.class]

[Contents](README.md) · up: [13.7 Template declarations \[temp.decls\]](temp.decls.md) · prev: [13.7 Template declarations \[temp.decls\]](temp.decls.md) · next: [13.7.1.1 Member functions of class templates \[temp.mem.func\]](temp.mem.func.md)

**1** A *class template* defines the layout and operations for an unbounded set of related types.

**2** [*Example 1*: A single class template `List` might provide an unbounded set of class definitions: one class `List<T>` for every type `T`, each describing a linked list of elements of type `T`. Similarly, a class template `Array` describing a contiguous, dynamic array might be defined like this:

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

The prefix `template<class T>` specifies that a template is being declared and that a *type-name* `T` may be used in the declaration. In other words, `Array` is a parameterized type with `T` as its parameter. — *end example*]

**3** When a member function, a member class, a member enumeration, a static data member or a member template of a class template is defined outside of the class template definition, the member definition is defined as a template definition in which the *template-head* is equivalent to that of the class template ([[temp.over.link]](temp.over.link.md)). The names of the template parameters used in the definition of the member may be different from the template parameter names used in the class template definition. The template argument list following the class template name in the member definition shall name the parameters in the same order as the one used in the template parameter list of the member. Each template parameter pack shall be expanded with an ellipsis in the template argument list. [*Example 2*:

```cpp
template<class T1, class T2> struct A {
  void f1();
  void f2();
};

template<class T2, class T1> void A<T2,T1>::f1() { }    // OK
template<class T2, class T1> void A<T1,T2>::f2() { }    // error
```

```cpp
template<class ... Types> struct B {
  void f3();
  void f4();
};

template<class ... Types> void B<Types ...>::f3() { }   // OK
template<class ... Types> void B<Types>::f4() { }       // error
```

```cpp
template<typename T> concept C = true;
template<typename T> concept D = true;

template<C T> struct S {
  void f();
  void g();
  void h();
  template<D U> struct Inner;
};

template<C A> void S<A>::f() { }        // OK: template-heads match
template<typename T> void S<T>::g() { } // error: no matching declaration for S<T>

template<typename T> requires C<T>      // ill-formed, no diagnostic required: template-heads are
void S<T>::h() { }                      // functionally equivalent but not equivalent

template<C X> template<D Y>
struct S<X>::Inner { };                 // OK
```

— *end example*]

**4** In a redeclaration, partial specialization, explicit specialization or explicit instantiation of a class template, the *class-key* shall agree in kind with the original class template declaration ([[dcl.type.elab]](dcl.type.elab.md)).

## Subclauses

- [13.7.1.1 Member functions of class templates \[temp.mem.func\]](temp.mem.func.md)
- [13.7.1.2 Deduction guides \[temp.deduct.guide\]](temp.deduct.guide.md)
- [13.7.1.3 Member classes of class templates \[temp.mem.class\]](temp.mem.class.md)
- [13.7.1.4 Static data members of class templates \[temp.static\]](temp.static.md)
- [13.7.1.5 Enumeration members of class templates \[temp.mem.enum\]](temp.mem.enum.md)
