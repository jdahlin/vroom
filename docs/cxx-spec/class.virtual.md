# 11.7.2 Virtual functions [class.virtual]

[Contents](README.md) · up: [11.7 Derived classes \[class.derived\]](class.derived.md) · prev: [11.7.1 Multiple base classes \[class.mi\]](class.mi.md) · next: [11.7.3 Abstract classes \[class.abstract\]](class.abstract.md)

**1** A non-static member function is a *virtual function* if it is first declared with the keyword `virtual` or if it overrides a virtual member function declared in a base class (see below).[^1] [*Note 1*: Virtual functions support dynamic binding and object-oriented programming. — *end note*] A class that declares or inherits a virtual function is called a *polymorphic class*.[^2]

**2** If a virtual member function `vf` is declared in a class `Base` and in a class `Derived`, derived directly or indirectly from `Base`, a member function `vf` with the same name, parameter-type-list ([[dcl.fct]](dcl.fct.md)), cv-qualification, and ref-qualifier (or absence of same) as `Base::vf` is declared, then `Derived::vf` *overrides*[^3] `Base::vf`. For convenience we say that any virtual function overrides itself. A virtual member function `C::vf` of a class object `S` is a *final overrider* unless the most derived class ([[intro.object]](intro.object.md)) of which `S` is a base class subobject (if any) declares or inherits another member function that overrides `vf`. In a derived class, if a virtual member function of a base class subobject has more than one final overrider the program is ill-formed. [*Example 1*:

```cpp
struct A {
  virtual void f();
};
struct B : virtual A {
  virtual void f();
};
struct C : B , virtual A {
  using A::f;
};

void foo() {
  C c;
  c.f();            // calls B::f, the final overrider
  c.C::f();         // calls A::f because of the using-declaration
}
```

— *end example*]

[*Example 2*:

```cpp
struct A { virtual void f(); };
struct B : A { };
struct C : A { void f(); };
struct D : B, C { };            // OK: A::f and C::f are the final overriders
                                // for the B and C subobjects, respectively
```

— *end example*]

**3** [*Note 2*: A virtual member function does not have to be visible to be overridden, for example,

```cpp
struct B {
  virtual void f();
};
struct D : B {
  void f(int);
};
struct D2 : D {
  void f();
};
```

the function `f(int)` in class `D` hides the virtual function `f()` in its base class `B`; `D::f(int)` is not a virtual function. However, `f()` declared in class `D2` has the same name and the same parameter list as `B::f()`, and therefore is a virtual function that overrides the function `B::f()` even though `B::f()` is not visible in class `D2`. — *end note*]

**4** If a virtual function `f` in some class `B` is marked with the *virt-specifier* `final` and in a class `D` derived from `B` a function `D::f` overrides `B::f`, the program is ill-formed. [*Example 3*:

```cpp
struct B {
  virtual void f() const final;
};

struct D : B {
  void f() const;   // error: D::f attempts to override final B::f
};
```

— *end example*]

**5** If a virtual function is marked with the *virt-specifier* `override` and does not override a member function of a base class, the program is ill-formed. [*Example 4*:

```cpp
struct B {
  virtual void f(int);
};

struct D : B {
  virtual void f(long) override;        // error: wrong signature overriding B::f
  virtual void f(int) override;         // OK
};
```

— *end example*]

**6** A virtual function shall not have a trailing *requires-clause* ([[dcl.decl]](dcl.decl.md)). [*Example 5*:

```cpp
struct A {
  virtual void f() requires true;       // error: virtual function cannot be constrained ([temp.constr.decl])
};
```

— *end example*]

**7** Even though destructors are not inherited, a destructor in a derived class overrides a base class destructor declared virtual; see [[class.dtor]](class.dtor.md) and [[class.free]](class.free.md).

**8** The return type of an overriding function shall be either identical to the return type of the overridden function or *covariant* with the classes of the functions. If a function `D::f` overrides a function `B::f`, the return types of the functions are covariant if they satisfy the following criteria:

- both are pointers to classes, both are lvalue references to classes, or both are rvalue references to classes[^4]
- the class in the return type of `B::f` is the same class as the class in the return type of `D::f`, or is an unambiguous and accessible direct or indirect base class of the class in the return type of `D::f`
- both pointers or references have the same cv-qualification and the class type in the return type of `D::f` has the same cv-qualification as or less cv-qualification than the class type in the return type of `B::f`.

**9** If the class type in the covariant return type of `D::f` differs from that of `B::f`, the class type in the return type of `D::f` shall be complete at the point of declaration of `D::f` or shall be the class type `D`. When the overriding function is called as the final overrider of the overridden function, its result is converted to the type returned by the (statically chosen) overridden function ([[expr.call]](expr.call.md)). [*Example 6*:

```cpp
class B { };
class D : private B { friend class Derived; };
struct Base {
  virtual void vf1();
  virtual void vf2();
  virtual void vf3();
  virtual B*   vf4();
  virtual B*   vf5();
  void f();
};

struct No_good : public Base {
  D*  vf4();        // error: B (base class of D) inaccessible
};

class A;
struct Derived : public Base {
    void vf1();     // virtual and overrides Base::vf1()
    void vf2(int);  // not virtual, hides Base::vf2()
    char vf3();     // error: invalid difference in return type only
    D*   vf4();     // OK: returns pointer to derived class
    A*   vf5();     // error: returns pointer to incomplete class
    void f();
};

void g() {
  Derived d;
  Base* bp = &d;                // standard conversion:
                                // Derived* to Base*
  bp->vf1();                    // calls Derived::vf1()
  bp->vf2();                    // calls Base::vf2()
  bp->f();                      // calls Base::f() (not virtual)
  B*  p = bp->vf4();            // calls Derived::vf4() and converts the
                                // result to B*
  Derived*  dp = &d;
  D*  q = dp->vf4();            // calls Derived::vf4() and does not
                                // convert the result to B*
  dp->vf2();                    // error: argument mismatch
}
```

— *end example*]

**10** [*Note 3*: The interpretation of the call of a virtual function depends on the type of the object for which it is called (the dynamic type), whereas the interpretation of a call of a non-virtual member function depends only on the type of the pointer or reference denoting that object (the static type) ([[expr.call]](expr.call.md)). — *end note*]

**11** [*Note 4*: The `virtual` specifier implies membership, so a virtual function cannot be a non-member ([[dcl.fct.spec]](dcl.fct.spec.md)) function. Nor can a virtual function be a static member, since a virtual function call relies on a specific object for determining which function to invoke. A virtual function declared in one class can be declared a friend ([[class.friend]](class.friend.md)) in another class. — *end note*]

**12** A virtual function declared in a class shall be defined, or declared pure ([[class.abstract]](class.abstract.md)) in that class, or both; no diagnostic is required ([[basic.def.odr]](basic.def.odr.md)).

**13** [*Example 7*: Here are some uses of virtual functions with multiple base classes:

```cpp
struct A {
  virtual void f();
};

struct B1 : A {                 // note non-virtual derivation
  void f();
};

struct B2 : A {
  void f();
};

struct D : B1, B2 {             // D has two separate A subobjects
};

void foo() {
  D   d;
//A* ap = &d; // would be ill-formed: ambiguous
  B1*  b1p = &d;
  A*   ap = b1p;
  D*   dp = &d;
  ap->f();                      // calls D::B1::f
  dp->f();                      // error: ambiguous
}
```

In class `D` above there are two occurrences of class `A` and hence two occurrences of the virtual member function `A::f`. The final overrider of `B1::A::f` is `B1::f` and the final overrider of `B2::A::f` is `B2::f`. — *end example*]

**14** [*Example 8*: The following example shows a function that does not have a unique final overrider:

```cpp
struct A {
  virtual void f();
};

struct VB1 : virtual A {        // note virtual derivation
  void f();
};

struct VB2 : virtual A {
  void f();
};

struct Error : VB1, VB2 {       // error
};

struct Okay : VB1, VB2 {
  void f();
};
```

Both `VB1::f` and `VB2::f` override `A::f` but there is no overrider of both of them in class `Error`. This example is therefore ill-formed. Class `Okay` is well-formed, however, because `Okay::f` is a final overrider. — *end example*]

**15** [*Example 9*: The following example uses the well-formed classes from above.

```cpp
struct VB1a : virtual A {       // does not declare f
};

struct Da : VB1a, VB2 {
};

void foe() {
  VB1a*  vb1ap = new Da;
  vb1ap->f();                   // calls VB2::f
}
```

— *end example*]

**16** Explicit qualification with the scope operator ([[expr.prim.id.qual]](expr.prim.id.qual.md)) suppresses the virtual call mechanism. [*Example 10*:

```cpp
class B { public: virtual void f(); };
class D : public B { public: void f(); };

void D::f() { /* ... */ B::f(); }
```

Here, the function call in `D::f` really does call `B::f` and not `D::f`. — *end example*]

**17** A function with a deleted definition ([[dcl.fct.def]](dcl.fct.def.md)) shall not override a function that does not have a deleted definition. Likewise, a function that does not have a deleted definition shall not override a function with a deleted definition.

**18** A `consteval` virtual function shall not override a virtual function that is not `consteval`. A `consteval` virtual function shall not be overridden by a virtual function that is not `consteval`.

[^1]: The use of the `virtual` specifier in the declaration of an overriding function is valid but redundant (has empty semantics).
[^2]: If all virtual functions are immediate functions, the class is still polymorphic even though its internal representation might not otherwise require any additions for that polymorphic behavior.
[^3]: A function with the same name but a different parameter list ([[over]](over.md)) as a virtual function is not necessarily virtual and does not override. Access control ([[class.access]](class.access.md)) is not considered in determining overriding.
[^4]: Multi-level pointers to classes or references to multi-level pointers to classes are not allowed.
