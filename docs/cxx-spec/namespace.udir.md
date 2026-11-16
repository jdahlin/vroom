# 9.8.3 Using namespace directive [namespace.udir]

[Contents](README.md) · up: [9.8 Namespaces \[basic.namespace\]](basic.namespace.md) · prev: [9.8.2 Namespace alias \[namespace.alias\]](namespace.alias.md) · next: [9.9 The `using` declaration \[namespace.udecl\]](namespace.udecl.md)

```bnf
using-directive:
    attribute-specifier-seqₒₚₜ 'using' 'namespace' nested-name-specifierₒₚₜ namespace-name ';'
```

**1** A *using-directive* shall not appear in class scope, but may appear in namespace scope or in block scope. [*Note 1*: When looking up a *namespace-name* in a *using-directive*, only namespace names are considered, see [[basic.lookup.udir]](basic.lookup.udir.md). — *end note*] The optional *attribute-specifier-seq* appertains to the *using-directive*.

**2** A *using-directive* specifies that the names in the nominated namespace can be used in the scope in which the *using-directive* appears after the *using-directive*. During unqualified name lookup ([[basic.lookup.unqual]](basic.lookup.unqual.md)), the names appear as if they were declared in the nearest enclosing namespace which contains both the *using-directive* and the nominated namespace. [*Note 2*: In this context, “contains” means “contains directly or indirectly”. — *end note*]

**3** A *using-directive* does not add any members to the declarative region in which it appears. [*Example 1*:

```cpp
namespace A {
  int i;
  namespace B {
    namespace C {
      int i;
    }
    using namespace A::B::C;
    void f1() {
      i = 5;        // OK, C::i visible in B and hides A::i
    }
  }
  namespace D {
    using namespace B;
    using namespace C;
    void f2() {
      i = 5;        // ambiguous, B::C::i or A::i?
    }
  }
  void f3() {
    i = 5;          // uses A::i
  }
}
void f4() {
  i = 5;            // error: neither i is visible
}
```

— *end example*]

**4** For unqualified lookup ([[basic.lookup.unqual]](basic.lookup.unqual.md)), the *using-directive* is transitive: if a scope contains a *using-directive* that nominates a second namespace that itself contains *using-directive*s, the effect is as if the *using-directive*s from the second namespace also appeared in the first. [*Note 3*: For qualified lookup, see [[namespace.qual]](namespace.qual.md). — *end note*] [*Example 2*:

```cpp
namespace M {
  int i;
}

namespace N {
  int i;
  using namespace M;
}

void f() {
  using namespace N;
  i = 7;            // error: both M::i and N::i are visible
}
```

For another example,

```cpp
namespace A {
  int i;
}
namespace B {
  int i;
  int j;
  namespace C {
    namespace D {
      using namespace A;
      int j;
      int k;
      int a = i;    // B::i hides A::i
    }
    using namespace D;
    int k = 89;     // no problem yet
    int l = k;      // ambiguous: C::k or D::k
    int m = i;      // B::i hides A::i
    int n = j;      // D::j hides B::j
  }
}
```

— *end example*]

**5** If a namespace is extended ([[namespace.def]](namespace.def.md)) after a *using-directive* for that namespace is given, the additional members of the extended namespace and the members of namespaces nominated by *using-directive*s in the extending *namespace-definition* can be used after the extending *namespace-definition*.

**6** [*Note 4*: If name lookup finds a declaration for a name in two different namespaces, and the declarations do not declare the same entity and do not declare functions or function templates, the use of the name is ill-formed ([[basic.lookup]](basic.lookup.md)). In particular, the name of a variable, function or enumerator does not hide the name of a class or enumeration declared in a different namespace. For example,

```cpp
namespace A {
  class X { };
  extern "C"   int g();
  extern "C++" int h();
}
namespace B {
  void X(int);
  extern "C"   int g();
  extern "C++" int h(int);
}
using namespace A;
using namespace B;

void f() {
  X(1);             // error: name X found in two namespaces
  g();              // OK: name g refers to the same entity
  h();              // OK: overload resolution selects A::h
}
```

— *end note*]

**7** During overload resolution, all functions from the transitive search are considered for argument matching. The set of declarations found by the transitive search is unordered. [*Note 5*: In particular, the order in which namespaces were considered and the relationships among the namespaces implied by the *using-directive*s do not cause preference to be given to any of the declarations found by the search. — *end note*] An ambiguity exists if the best match finds two functions with the same signature, even if one is in a namespace reachable through *using-directive*s in the namespace of the other.[^1] [*Example 3*:

```cpp
namespace D {
  int d1;
  void f(char);
}
using namespace D;

int d1;             // OK: no conflict with D::d1

namespace E {
  int e;
  void f(int);
}

namespace D {       // namespace extension
  int d2;
  using namespace E;
  void f(int);
}

void f() {
  d1++;             // error: ambiguous ::d1 or D::d1?
  ::d1++;           // OK
  D::d1++;          // OK
  d2++;             // OK: D::d2
  e++;              // OK: E::e
  f(1);             // error: ambiguous: D::f(int) or E::f(int)?
  f('a');           // OK: D::f(char)
}
```

— *end example*]

[^1]: During name lookup in a class hierarchy, some ambiguities may be resolved by considering whether one member hides the other along some paths ([[class.member.lookup]](class.member.lookup.md)). There is no such disambiguation when considering the set of names found as a result of following *using-directive*s.
