# 9.9 The `using` declaration [namespace.udecl]

[Contents](README.md) · up: [9 Declarations \[dcl.dcl\]](dcl.dcl.md) · prev: [9.8.3 Using namespace directive \[namespace.udir\]](namespace.udir.md) · next: [9.10 The `asm` declaration \[dcl.asm\]](dcl.asm.md)

```bnf
using-declaration:
    'using' using-declarator-list ';'
```

```bnf
using-declarator-list:
    using-declarator '...'ₒₚₜ
    using-declarator-list ',' using-declarator '...'ₒₚₜ
```

```bnf
using-declarator:
    'typename'ₒₚₜ nested-name-specifier unqualified-id
```

**1** Each *using-declarator* in a *using-declaration*[^1] introduces a set of declarations into the declarative region in which the *using-declaration* appears. The set of declarations introduced by the *using-declarator* is found by performing qualified name lookup ([[basic.lookup.qual]](basic.lookup.qual.md), [[class.member.lookup]](class.member.lookup.md)) for the name in the *using-declarator*, excluding functions that are hidden as described below. If the *using-declarator* does not name a constructor, the *unqualified-id* is declared in the declarative region in which the *using-declaration* appears as a synonym for each declaration introduced by the *using-declarator*. [*Note 1*: Only the specified name is so declared; specifying an enumeration name in a *using-declaration* does not declare its enumerators in the *using-declaration*’s declarative region. — *end note*] If the *using-declarator* names a constructor, it declares that the class *inherits* the set of constructor declarations introduced by the *using-declarator* from the nominated base class.

**2** Every *using-declaration* is a *declaration* and a *member-declaration* and can therefore be used in a class definition. [*Example 1*:

```cpp
struct B {
  void f(char);
  void g(char);
  enum E { e };
  union { int x; };
};

struct D : B {
  using B::f;
  void f(int) { f('c'); }       // calls B::f(char)
  void g(int) { g('c'); }       // recursively calls D::g(int)
};
```

— *end example*]

**3** In a *using-declaration* used as a *member-declaration*, each *using-declarator* shall either name an enumerator or have a *nested-name-specifier* naming a base class of the class being defined. [*Example 2*:

```cpp
enum class button { up, down };
struct S {
  using button::up;
  button b = up;                // OK
};
```

— *end example*] If a *using-declarator* names a constructor, its *nested-name-specifier* shall name a direct base class of the class being defined. [*Example 3*:

```cpp
template <typename... bases>
struct X : bases... {
  using bases::g...;
};

X<B, D> x;                      // OK: B::g and D::g introduced
```

— *end example*] [*Example 4*:

```cpp
class C {
  int g();
};

class D2 : public B {
  using B::f;                   // OK: B is a base of D2
  using B::e;                   // OK: e is an enumerator of base B
  using B::x;                   // OK: x is a union member of base B
  using C::g;                   // error: C isn't a base of D2
};
```

— *end example*]

**4** [*Note 2*: Since destructors do not have names, a *using-declaration* cannot refer to a destructor for a base class. Since specializations of member templates for conversion functions are not found by name lookup, they are not considered when a *using-declaration* specifies a conversion function ([[temp.mem]](temp.mem.md)). — *end note*] If a constructor or assignment operator brought from a base class into a derived class has the signature of a copy/move constructor or assignment operator for the derived class ([[class.copy.ctor]](class.copy.ctor.md), [[class.copy.assign]](class.copy.assign.md)), the *using-declaration* does not by itself suppress the implicit declaration of the derived class member; the member from the base class is hidden or overridden by the implicitly-declared copy/move constructor or assignment operator of the derived class, as described below.

**5** A *using-declaration* shall not name a *template-id*. [*Example 5*:

```cpp
struct A {
  template <class T> void f(T);
  template <class T> struct X { };
};
struct B : A {
  using A::f<double>;           // error
  using A::X<int>;              // error
};
```

— *end example*]

**6** A *using-declaration* shall not name a namespace.

**7** A *using-declaration* that names a class member other than an enumerator shall be a *member-declaration*. [*Example 6*:

```cpp
struct X {
  int i;
  static int s;
};

void f() {
  using X::i;                   // error: X::i is a class member and this is not a member declaration.
  using X::s;                   // error: X::s is a class member and this is not a member declaration.
}
```

— *end example*]

**8** Members declared by a *using-declaration* can be referred to by explicit qualification just like other member names ([[namespace.qual]](namespace.qual.md)). [*Example 7*:

```cpp
void f();

namespace A {
  void g();
}

namespace X {
  using ::f;                    // global f
  using A::g;                   // A's g
}

void h()
{
  X::f();                       // calls ::f
  X::g();                       // calls A::g
}
```

— *end example*]

**9** A *using-declaration* is a *declaration* and can therefore be used repeatedly where (and only where) multiple declarations are allowed. [*Example 8*:

```cpp
namespace A {
  int i;
}

namespace A1 {
  using A::i, A::i;             // OK: double declaration
}

struct B {
  int i;
};

struct X : B {
  using B::i, B::i;             // error: double member declaration
};
```

— *end example*]

**10** [*Note 3*: For a *using-declaration* whose *nested-name-specifier* names a namespace, members added to the namespace after the *using-declaration* are not in the set of introduced declarations, so they are not considered when a use of the name is made. Thus, additional overloads added after the *using-declaration* are ignored, but default function arguments ([[dcl.fct.default]](dcl.fct.default.md)), default template arguments ([[temp.param]](temp.param.md)), and template specializations ([[temp.class.spec]](temp.class.spec.md), [[temp.expl.spec]](temp.expl.spec.md)) are considered. — *end note*] [*Example 9*:

```cpp
namespace A {
  void f(int);
}

using A::f;         // f is a synonym for A::f; that is, for A::f(int).
namespace A {
  void f(char);
}

void foo() {
  f('a');           // calls f(int), even though f(char) exists.
}

void bar() {
  using A::f;       // f is a synonym for A::f; that is, for A::f(int) and A::f(char).
  f('a');           // calls f(char)
}
```

— *end example*]

**11** [*Note 4*: Partial specializations of class templates are found by looking up the primary class template and then considering all partial specializations of that template. If a *using-declaration* names a class template, partial specializations introduced after the *using-declaration* are effectively visible because the primary template is visible ([[temp.class.spec]](temp.class.spec.md)). — *end note*]

**12** Since a *using-declaration* is a declaration, the restrictions on declarations of the same name in the same declarative region ([[basic.scope]](basic.scope.md)) also apply to *using-declaration*s. [*Example 10*:

```cpp
namespace A {
  int x;
}

namespace B {
  int i;
  struct g { };
  struct x { };
  void f(int);
  void f(double);
  void g(char);     // OK: hides struct g
}

void func() {
  int i;
  using B::i;       // error: i declared twice
  void f(char);
  using B::f;       // OK: each f is a function
  f(3.5);           // calls B::f(double)
  using B::g;
  g('a');           // calls B::g(char)
  struct g g1;      // g1 has class type B::g
  using B::x;
  using A::x;       // OK: hides struct B::x
  x = 99;           // assigns to A::x
  struct x x1;      // x1 has class type B::x
}
```

— *end example*]

**13** If a function declaration in namespace scope or block scope has the same name and the same parameter-type-list ([[dcl.fct]](dcl.fct.md)) as a function introduced by a *using-declaration*, and the declarations do not declare the same function, the program is ill-formed. If a function template declaration in namespace scope has the same name, parameter-type-list, trailing *requires-clause* (if any), return type, and *template-head*, as a function template introduced by a *using-declaration*, the program is ill-formed. [*Note 5*: Two *using-declaration*s may introduce functions with the same name and the same parameter-type-list. If, for a call to an unqualified function name, function overload resolution selects the functions introduced by such *using-declaration*s, the function call is ill-formed. [*Example 11*:

```cpp
namespace B {
  void f(int);
  void f(double);
}
namespace C {
  void f(int);
  void f(double);
  void f(char);
}

void h() {
  using B::f;       // B::f(int) and B::f(double)
  using C::f;       // C::f(int), C::f(double), and C::f(char)
  f('h');           // calls C::f(char)
  f(1);             // error: ambiguous: B::f(int) or C::f(int)?
  void f(int);      // error: f(int) conflicts with C::f(int) and B::f(int)
}
```

— *end example*] — *end note*]

**14** When a *using-declarator* brings declarations from a base class into a derived class, member functions and member function templates in the derived class override and/or hide member functions and member function templates with the same name, parameter-type-list ([[dcl.fct]](dcl.fct.md)), trailing *requires-clause* (if any), cv-qualification, and *ref-qualifier* (if any), in a base class (rather than conflicting). Such hidden or overridden declarations are excluded from the set of declarations introduced by the *using-declarator*. [*Example 12*:

```cpp
struct B {
  virtual void f(int);
  virtual void f(char);
  void g(int);
  void h(int);
};

struct D : B {
  using B::f;
  void f(int);      // OK: D::f(int) overrides B::f(int);

  using B::g;
  void g(char);     // OK

  using B::h;
  void h(int);      // OK: D::h(int) hides B::h(int)
};

void k(D* p)
{
  p->f(1);          // calls D::f(int)
  p->f('a');        // calls B::f(char)
  p->g(1);          // calls B::g(int)
  p->g('a');        // calls D::g(char)
}

struct B1 {
  B1(int);
};

struct B2 {
  B2(int);
};

struct D1 : B1, B2 {
  using B1::B1;
  using B2::B2;
};
D1 d1(0);           // error: ambiguous

struct D2 : B1, B2 {
  using B1::B1;
  using B2::B2;
  D2(int);          // OK: D2::D2(int) hides B1::B1(int) and B2::B2(int)
};
D2 d2(0);           // calls D2::D2(int)
```

— *end example*]

**15** [*Note 6*: For the purpose of forming a set of candidates during overload resolution, the functions that are introduced by a *using-declaration* into a derived class are treated as though they were members of the derived class ([[class.member.lookup]](class.member.lookup.md)). In particular, the implicit object parameter is treated as if it were a reference to the derived class rather than to the base class ([[over.match.funcs]](over.match.funcs.md)). This has no effect on the type of the function, and in all other respects the function remains a member of the base class. — *end note*]

**16** Constructors that are introduced by a *using-declaration* are treated as though they were constructors of the derived class when looking up the constructors of the derived class ([[class.qual]](class.qual.md)) or forming a set of overload candidates ([[over.match.ctor]](over.match.ctor.md), [[over.match.copy]](over.match.copy.md), [[over.match.list]](over.match.list.md)). [*Note 7*: If such a constructor is selected to perform the initialization of an object of class type, all subobjects other than the base class from which the constructor originated are implicitly initialized ([[class.inhctor.init]](class.inhctor.init.md)). A constructor of a derived class is sometimes preferred to a constructor of a base class if they would otherwise be ambiguous ([[over.match.best]](over.match.best.md)). — *end note*]

**17** In a *using-declarator* that does not name a constructor, all members of the set of introduced declarations shall be accessible. In a *using-declarator* that names a constructor, no access check is performed. In particular, if a derived class uses a *using-declarator* to access a member of a base class, the member name shall be accessible. If the name is that of an overloaded member function, then all functions named shall be accessible. The base class members mentioned by a *using-declarator* shall be visible in the scope of at least one of the direct base classes of the class where the *using-declarator* is specified.

**18** [*Note 8*: Because a *using-declarator* designates a base class member (and not a member subobject or a member function of a base class subobject), a *using-declarator* cannot be used to resolve inherited member ambiguities. [*Example 13*:

```cpp
struct A { int x(); };
struct B : A { };
struct C : A {
  using A::x;
  int x(int);
};

struct D : B, C {
  using C::x;
  int x(double);
};
int f(D* d) {
  return d->x();    // error: overload resolution selects A::x, but A is an ambiguous base class
}
```

— *end example*] — *end note*]

**19** A synonym created by a *using-declaration* has the usual accessibility for a *member-declaration*. A *using-declarator* that names a constructor does not create a synonym; instead, the additional constructors are accessible if they would be accessible when used to construct an object of the corresponding base class, and the accessibility of the *using-declaration* is ignored. [*Example 14*:

```cpp
class A {
private:
    void f(char);
public:
    void f(int);
protected:
    void g();
};

class B : public A {
  using A::f;       // error: A::f(char) is inaccessible
public:
  using A::g;       // B::g is a public synonym for A::g
};
```

— *end example*]

**20** If a *using-declarator* uses the keyword `typename` and specifies a dependent name ([[temp.dep]](temp.dep.md)), the name introduced by the *using-declaration* is treated as a *typedef-name* ([[dcl.typedef]](dcl.typedef.md)).

[^1]: A *using-declaration* with more than one *using-declarator* is equivalent to a corresponding sequence of *using-declaration*s with one *using-declarator* each.
