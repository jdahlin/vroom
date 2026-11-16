# 6.4.7 Class scope [basic.scope.class]

[Contents](README.md) · up: [6.4 Scope \[basic.scope\]](basic.scope.md) · prev: [6.4.6 Namespace scope \[basic.scope.namespace\]](basic.scope.namespace.md) · next: [6.4.8 Enumeration scope \[basic.scope.enum\]](basic.scope.enum.md)

**1** The potential scope of a name declared in a class consists not only of the declarative region following the name’s point of declaration, but also of all complete-class contexts ([[class.mem]](class.mem.md)) of that class.

**2** A name `N` used in a class `S` shall refer to the same declaration in its context and when re-evaluated in the completed scope of `S`. No diagnostic is required for a violation of this rule.

**3** A name declared within a member function hides a declaration of the same name whose scope extends to or past the end of the member function’s class.

**4** The potential scope of a declaration in a class that extends to or past the end of a class definition also extends to the regions defined by its member definitions, even if the members are defined lexically outside the class (this includes static data member definitions, nested class definitions, and member function definitions, including the member function body and any portion of the declarator part of such definitions which follows the *declarator-id*, including a *parameter-declaration-clause* and any default arguments ([[dcl.fct.default]](dcl.fct.default.md))).

**5** [*Example 1*:

```cpp
typedef int  c;
enum { i = 1 };

class X {
  char  v[i];                       // error: i refers to ::i but when reevaluated is X::i
  int  f() { return sizeof(c); }    // OK: X::c
  char  c;
  enum { i = 2 };
};

typedef char*  T;
struct Y {
  T  a;                             // error: T refers to ::T but when reevaluated is Y::T
  typedef long  T;
  T  b;
};

typedef int I;
class D {
  typedef I I;                      // error, even though no reordering involved
};
```

— *end example*]

**6** The name of a class member shall only be used as follows:

- in the scope of its class (as described above) or a class derived ([[class.derived]](class.derived.md)) from its class,
- after the `.` operator applied to an expression of the type of its class ([[expr.ref]](expr.ref.md)) or a class derived from its class,
- after the `->` operator applied to a pointer to an object of its class ([[expr.ref]](expr.ref.md)) or a class derived from its class,
- after the `::` scope resolution operator ([[expr.prim.id.qual]](expr.prim.id.qual.md)) applied to the name of its class or a class derived from its class.
