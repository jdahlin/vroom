# 9.8.1.2 Namespace member definitions [namespace.memdef]

[Contents](README.md) · up: [9.8.1 Namespace definition \[namespace.def\]](namespace.def.md) · prev: [9.8.1.1 Unnamed namespaces \[namespace.unnamed\]](namespace.unnamed.md) · next: [9.8.2 Namespace alias \[namespace.alias\]](namespace.alias.md)

**1** A declaration in a namespace `N` (excluding declarations in nested scopes) whose *declarator-id* is an *unqualified-id* ([[dcl.meaning]](dcl.meaning.md)), whose *class-head-name* ([[class.pre]](class.pre.md)) or *enum-head-name* ([[dcl.enum]](dcl.enum.md)) is an *identifier*, or whose *elaborated-type-specifier* is of the form *class-key* *attribute-specifier-seq*ₒₚₜ *identifier* ([[dcl.type.elab]](dcl.type.elab.md)), or that is an *opaque-enum-declaration*, declares (or redeclares) its *unqualified-id* or *identifier* as a member of `N`. [*Note 1*: An explicit instantiation ([[temp.explicit]](temp.explicit.md)) or explicit specialization ([[temp.expl.spec]](temp.expl.spec.md)) of a template does not introduce a name and thus may be declared using an *unqualified-id* in a member of the enclosing namespace set, if the primary template is declared in an inline namespace. — *end note*] [*Example 1*:

```cpp
namespace X {
  void f() { /* ... */ }        // OK: introduces X::f()

  namespace M {
    void g();                   // OK: introduces X::M::g()
  }
  using M::g;
  void g();                     // error: conflicts with X::M::g()
}
```

— *end example*]

**2** Members of a named namespace can also be defined outside that namespace by explicit qualification ([[namespace.qual]](namespace.qual.md)) of the name being defined, provided that the entity being defined was already declared in the namespace and the definition appears after the point of declaration in a namespace that encloses the declaration’s namespace. [*Example 2*:

```cpp
namespace Q {
  namespace V {
    void f();
  }
  void V::f() { /* ... */ }     // OK
  void V::g() { /* ... */ }     // error: g() is not yet a member of V
  namespace V {
    void g();
  }
}

namespace R {
  void Q::V::g() { /* ... */ }  // error: R doesn't enclose Q
}
```

— *end example*]

**3** If a friend declaration in a non-local class first declares a class, function, class template or function template[^1] the friend is a member of the innermost enclosing namespace. The friend declaration does not by itself make the name visible to unqualified lookup ([[basic.lookup.unqual]](basic.lookup.unqual.md)) or qualified lookup ([[basic.lookup.qual]](basic.lookup.qual.md)). [*Note 2*: The name of the friend will be visible in its namespace if a matching declaration is provided at namespace scope (either before or after the class definition granting friendship). — *end note*] If a friend function or function template is called, its name may be found by the name lookup that considers functions from namespaces and classes associated with the types of the function arguments ([[basic.lookup.argdep]](basic.lookup.argdep.md)). If the name in a friend declaration is neither qualified nor a *template-id* and the declaration is a function or an *elaborated-type-specifier*, the lookup to determine whether the entity has been previously declared shall not consider any scopes outside the innermost enclosing namespace. [*Note 3*: The other forms of friend declarations cannot declare a new member of the innermost enclosing namespace and thus follow the usual lookup rules. — *end note*] [*Example 3*:

```cpp
// Assume f and g have not yet been declared.
void h(int);
template <class T> void f2(T);
namespace A {
  class X {
    friend void f(X);           // A::f(X) is a friend
    class Y {
      friend void g();          // A::g is a friend
      friend void h(int);       // A::h is a friend
                                // ::h not considered
      friend void f2<>(int);    // ::f2<>(int) is a friend
    };
  };

  // A::f, A::g and A::h are not visible here
  X x;
  void g() { f(x); }            // definition of A::g
  void f(X) { /* ... */ }       // definition of A::f
  void h(int) { /* ... */ }     // definition of A::h
  // A::f, A::g and A::h are visible here and known to be friends
}

using A::x;

void h() {
  A::f(x);
  A::X::f(x);                   // error: f is not a member of A::X
  A::X::Y::g();                 // error: g is not a member of A::X::Y
}
```

— *end example*]

[^1]: this implies that the name of the class or function is unqualified.
