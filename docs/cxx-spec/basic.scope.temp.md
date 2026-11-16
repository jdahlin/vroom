# 6.4.9 Template parameter scope [basic.scope.temp]

[Contents](README.md) · up: [6.4 Scope \[basic.scope\]](basic.scope.md) · prev: [6.4.8 Enumeration scope \[basic.scope.enum\]](basic.scope.enum.md) · next: [6.4.10 Name hiding \[basic.scope.hiding\]](basic.scope.hiding.md)

**1** The declarative region of the name of a template parameter of a template *template-parameter* is the smallest *template-parameter-list* in which the name was introduced.

**2** The declarative region of the name of a template parameter of a template is the smallest *template-declaration* in which the name was introduced. Only template parameter names belong to this declarative region; any other kind of name introduced by the *declaration* of a *template-declaration* is instead introduced into the same declarative region where it would be introduced as a result of a non-template declaration of the same name. [*Example 1*:

```cpp
namespace N {
  template<class T> struct A { };               // #1
  template<class U> void f(U) { }               // #2
  struct B {
    template<class V> friend int g(struct C*);  // #3
  };
}
```

The declarative regions of `T`, `U` and `V` are the *template-declaration*s on lines #1, #2, and #3, respectively. But the names `A`, `f`, `g` and `C` all belong to the same declarative region — namely, the *namespace-body* of `N`. (`g` is still considered to belong to this declarative region in spite of its being hidden during qualified and unqualified name lookup.) — *end example*]

**3** The potential scope of a template parameter name begins at its point of declaration ([[basic.scope.pdecl]](basic.scope.pdecl.md)) and ends at the end of its declarative region. [*Note 1*: This implies that a *template-parameter* can be used in the declaration of subsequent *template-parameter*s and their default arguments but cannot be used in preceding *template-parameter*s or their default arguments. For example,

```cpp
template<class T, T* p, class U = T> class X { /* ... */ };
template<class T> void f(T* p = new T);
```

This also implies that a *template-parameter* can be used in the specification of base classes. For example,

```cpp
template<class T> class X : public Array<T> { /* ... */ };
template<class T> class Y : public T { /* ... */ };
```

The use of a template parameter as a base class implies that a class used as a template argument must be defined and not just declared when the class template is instantiated. — *end note*]

**4** The declarative region of the name of a template parameter is nested within the immediately-enclosing declarative region. [*Note 2*: As a result, a *template-parameter* hides any entity with the same name in an enclosing scope ([[basic.scope.hiding]](basic.scope.hiding.md)). [*Example 2*:

```cpp
typedef int N;
template<N X, typename N, template<N Y> class T> struct A;
```

Here, `X` is a non-type template parameter of type `int` and `Y` is a non-type template parameter of the same type as the second template parameter of `A`. — *end example*] — *end note*]

**5** [*Note 3*: Because the name of a template parameter cannot be redeclared within its potential scope ([[temp.local]](temp.local.md)), a template parameter’s scope is often its potential scope. However, it is still possible for a template parameter name to be hidden; see [[temp.local]](temp.local.md). — *end note*]
