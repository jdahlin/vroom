# 13.4 Template arguments [temp.arg]

[Contents](../README.md) · up: [13 Templates \[temp\]](temp.md) · prev: [13.3 Names of template specializations \[temp.names\]](temp.names.md) · next: [13.4.1 Template type arguments \[temp.arg.type\]](temp.arg.type.md)

**1** There are three forms of *template-argument*, corresponding to the three forms of *template-parameter*: type, non-type and template. The type and form of each *template-argument* specified in a *template-id* shall match the type and form specified for the corresponding parameter declared by the template in its *template-parameter-list*. When the parameter declared by the template is a template parameter pack ([[temp.variadic]](temp.variadic.md)), it will correspond to zero or more *template-argument*s. [*Example 1*:

```cpp
template<class T> class Array {
  T* v;
  int sz;
public:
  explicit Array(int);
  T& operator[](int);
  T& elem(int i) { return v[i]; }
};

Array<int> v1(20);
typedef std::complex<double> dcomplex;  // std::complex is a standard library template
Array<dcomplex> v2(30);
Array<dcomplex> v3(40);

void bar() {
  v1[3] = 7;
  v2[3] = v3.elem(4) = dcomplex(7,8);
}
```

— *end example*]

**2** In a *template-argument*, an ambiguity between a *type-id* and an expression is resolved to a *type-id*, regardless of the form of the corresponding *template-parameter*.[^1] [*Example 2*:

```cpp
template<class T> void f();
template<int I> void f();

void g() {
  f<int()>();       // int() is a type-id: call the first f()
}
```

— *end example*]

**3** The name of a *template-argument* shall be accessible at the point where it is used as a *template-argument*. [*Note 1*: If the name of the *template-argument* is accessible at the point where it is used as a *template-argument*, there is no further access restriction in the resulting instantiation where the corresponding *template-parameter* name is used. — *end note*] [*Example 3*:

```cpp
template<class T> class X {
  static T t;
};

class Y {
private:
  struct S { /* ... */ };
  X<S> x;           // OK: S is accessible
                    // X<Y::S> has a static member of type Y::S
                    // OK: even though Y::S is private
};

X<Y::S> y;          // error: S not accessible
```

— *end example*] For a *template-argument* that is a class type or a class template, the template definition has no special access rights to the members of the *template-argument*. [*Example 4*:

```cpp
template <template <class TT> class T> class A {
  typename T<int>::S s;
};

template <class U> class B {
private:
  struct S { /* ... */ };
};

A<B> b;             // error: A has no access to B::S
```

— *end example*]

**4** When template argument packs or default *template-argument*s are used, a *template-argument* list can be empty. In that case the empty `<>` brackets shall still be used as the *template-argument-list*. [*Example 5*:

```cpp
template<class T = char> class String;
String<>* p;                    // OK: String<char>
String* q;                      // syntax error
template<class ... Elements> class Tuple;
Tuple<>* t;                     // OK: Elements is empty
Tuple* u;                       // syntax error
```

— *end example*]

**5** An explicit destructor call ([[class.dtor]](../class/class.dtor.md)) for an object that has a type that is a class template specialization may explicitly specify the *template-argument*s. [*Example 6*:

```cpp
template<class T> struct A {
  ~A();
};
void f(A<int>* p, A<int>* q) {
  p->A<int>::~A();              // OK: destructor call
  q->A<int>::~A<int>();         // OK: destructor call
}
```

— *end example*]

**6** If the use of a *template-argument* gives rise to an ill-formed construct in the instantiation of a template specialization, the program is ill-formed.

**7** When name lookup for the name in a *template-id* finds an overload set, both non-template functions in the overload set and function templates in the overload set for which the *template-argument*s do not match the *template-parameter*s are ignored. If none of the function templates have matching *template-parameter*s, the program is ill-formed.

**8** When a *simple-template-id* does not name a function, a default *template-argument* is implicitly instantiated ([[temp.inst]](temp.inst.md)) when the value of that default argument is needed. [*Example 7*:

```cpp
template<typename T, typename U = int> struct S { };
S<bool>* p;         // the type of p is S<bool, int>*
```

The default argument for `U` is instantiated to form the type `S<bool, int>*`. — *end example*]

**9** A *template-argument* followed by an ellipsis is a pack expansion ([[temp.variadic]](temp.variadic.md)).

## Subclauses

- [13.4.1 Template type arguments \[temp.arg.type\]](temp.arg.type.md)
- [13.4.2 Template non-type arguments \[temp.arg.nontype\]](temp.arg.nontype.md)
- [13.4.3 Template template arguments \[temp.arg.template\]](temp.arg.template.md)

[^1]: There is no such ambiguity in a default *template-argument* because the form of the *template-parameter* determines the allowable forms of the *template-argument*.
