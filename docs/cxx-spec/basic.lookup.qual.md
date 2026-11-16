# 6.5.3 Qualified name lookup [basic.lookup.qual]

[Contents](README.md) · up: [6.5 Name lookup \[basic.lookup\]](basic.lookup.md) · prev: [6.5.2 Argument-dependent name lookup \[basic.lookup.argdep\]](basic.lookup.argdep.md) · next: [6.5.3.1 Class members \[class.qual\]](class.qual.md)

**1** The name of a class or namespace member or enumerator can be referred to after the `::` scope resolution operator ([[expr.prim.id.qual]](expr.prim.id.qual.md)) applied to a *nested-name-specifier* that denotes its class, namespace, or enumeration. If a `::` scope resolution operator in a *nested-name-specifier* is not preceded by a *decltype-specifier*, lookup of the name preceding that `::` considers only namespaces, types, and templates whose specializations are types. If the name found does not designate a namespace or a class, enumeration, or dependent type, the program is ill-formed. [*Example 1*:

```cpp
class A {
public:
  static int n;
};
int main() {
  int A;
  A::n = 42;        // OK
  A b;              // error: A does not name a type
}
```

— *end example*]

**2** [*Note 1*: Multiply qualified names, such as `N1::N2::N3::n`, can be used to refer to members of nested classes ([[class.nest]](class.nest.md)) or members of nested namespaces. — *end note*]

**3** In a declaration in which the *declarator-id* is a *qualified-id*, names used before the *qualified-id* being declared are looked up in the defining namespace scope; names following the *qualified-id* are looked up in the scope of the member’s class or namespace. [*Example 2*:

```cpp
class X { };
class C {
  class X { };
  static const int number = 50;
  static X arr[number];
};
X C::arr[number];   // error:
                    // equivalent to ::X C::arr[C::number];
                    // and not to C::X C::arr[C::number];
```

— *end example*]

**4** A name prefixed by the unary scope operator `::` ([[expr.prim.id.qual]](expr.prim.id.qual.md)) is looked up in global scope, in the translation unit where it is used. The name shall be declared in global namespace scope or shall be a name whose declaration is visible in global scope because of a *using-directive* ([[namespace.qual]](namespace.qual.md)). The use of `::` allows a global name to be referred to even if its identifier has been hidden ([[basic.scope.hiding]](basic.scope.hiding.md)).

**5** A name prefixed by a *nested-name-specifier* that nominates an enumeration type shall represent an *enumerator* of that enumeration.

**6** In a *qualified-id* of the form:

```bnf
nested-name-specifierₒₚₜ type-name '::' '~' type-name
```

the second *type-name* is looked up in the same scope as the first. [*Example 3*:

```cpp
struct C {
  typedef int I;
};
typedef int I1, I2;
extern int* p;
extern int* q;
p->C::I::~I();      // I is looked up in the scope of C
q->I1::~I2();       // I2 is looked up in the scope of the postfix-expression

struct A {
  ~A();
};
typedef A AB;
int main() {
  AB* p;
  p->AB::~AB();     // explicitly calls the destructor for A
}
```

— *end example*] [*Note 2*: [[basic.lookup.classref]](basic.lookup.classref.md) describes how name lookup proceeds after the `.` and `->` operators. — *end note*]

## Subclauses

- [6.5.3.1 Class members \[class.qual\]](class.qual.md)
- [6.5.3.2 Namespace members \[namespace.qual\]](namespace.qual.md)
