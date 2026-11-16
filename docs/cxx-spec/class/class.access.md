# 11.9 Member access control [class.access]

[Contents](../README.md) · up: [11 Classes \[class\]](class.md) · prev: [11.8 Member name lookup \[class.member.lookup\]](class.member.lookup.md) · next: [11.9.1 Access specifiers \[class.access.spec\]](class.access.spec.md)

**1** A member of a class can be

- private; that is, its name can be used only by members and friends of the class in which it is declared.
- protected; that is, its name can be used only by members and friends of the class in which it is declared, by classes derived from that class, and by their friends (see [[class.protected]](class.protected.md)).
- public; that is, its name can be used anywhere without access restriction.

**2** A member of a class can also access all the names to which the class has access. A local class of a member function may access the same names that the member function itself may access.[^1]

**3** Members of a class defined with the keyword `class` are `private` by default. Members of a class defined with the keywords `struct` or `union` are public by default. [*Example 1*:

```cpp
class X {
  int a;            // X::a is private by default
};

struct S {
  int a;            // S::a is public by default
};
```

— *end example*]

**4** Access control is applied uniformly to all names, whether the names are referred to from declarations or expressions. [*Note 1*: Access control applies to names nominated by friend declarations ([[class.friend]](class.friend.md)) and *using-declaration*s ([[namespace.udecl]](../dcl/namespace.udecl.md)). — *end note*] In the case of overloaded function names, access control is applied to the function selected by overload resolution. [*Note 2*: Because access control applies to names, if access control is applied to a typedef name, only the accessibility of the typedef name itself is considered. The accessibility of the entity referred to by the typedef is not considered. For example,

```cpp
class A {
  class B { };
public:
  typedef B BB;
};

void f() {
  A::BB x;          // OK, typedef name A::BB is public
  A::B y;           // access error, A::B is private
}
```

— *end note*]

**5** [*Note 3*: Access to members and base classes is controlled, not their visibility ([[basic.scope.hiding]](../basic/basic.scope.hiding.md)). Names of members are still visible, and implicit conversions to base classes are still considered, when those members and base classes are inaccessible. — *end note*] The interpretation of a given construct is established without regard to access control. If the interpretation established makes use of inaccessible member names or base classes, the construct is ill-formed.

**6** All access controls in [[class.access]](class.access.md) affect the ability to access a class member name from the declaration of a particular entity, including parts of the declaration preceding the name of the entity being declared and, if the entity is a class, the definitions of members of the class appearing outside the class’s *member-specification*. [*Note 4*: This access also applies to implicit references to constructors, conversion functions, and destructors. — *end note*]

**7** [*Example 2*:

```cpp
class A {
  typedef int I;    // private member
  I f();
  friend I g(I);
  static I x;
  template<int> struct Q;
  template<int> friend struct R;
protected:
    struct B { };
};

A::I A::f() { return 0; }
A::I g(A::I p = A::x);
A::I g(A::I p) { return 0; }
A::I A::x = 0;
template<A::I> struct A::Q { };
template<A::I> struct R { };

struct D: A::B, A { };
```

Here, all the uses of `A::I` are well-formed because `A::f`, `A::x`, and `A::Q` are members of class `A` and `g` and `R` are friends of class `A`. This implies, for example, that access checking on the first use of `A::I` must be deferred until it is determined that this use of `A::I` is as the return type of a member of class `A`. Similarly, the use of `A::B` as a *base-specifier* is well-formed because `D` is derived from `A`, so checking of *base-specifier*s must be deferred until the entire *base-specifier-list* has been seen. — *end example*]

**8** The names in a default argument ([[dcl.fct.default]](../dcl/dcl.fct.default.md)) are bound at the point of declaration, and access is checked at that point rather than at any points of use of the default argument. Access checking for default arguments in function templates and in member functions of class templates is performed as described in [[temp.inst]](../temp/temp.inst.md).

**9** The names in a default *template-argument* ([[temp.param]](../temp/temp.param.md)) have their access checked in the context in which they appear rather than at any points of use of the default *template-argument*. [*Example 3*:

```cpp
class B { };
template <class T> class C {
protected:
  typedef T TT;
};

template <class U, class V = typename U::TT>
class D : public U { };

D <C<B> >* d;       // access error, C::TT is protected
```

— *end example*]

## Subclauses

- [11.9.1 Access specifiers \[class.access.spec\]](class.access.spec.md)
- [11.9.2 Accessibility of base classes and base class members \[class.access.base\]](class.access.base.md)
- [11.9.3 Friends \[class.friend\]](class.friend.md)
- [11.9.4 Protected member access \[class.protected\]](class.protected.md)
- [11.9.5 Access to virtual functions \[class.access.virt\]](class.access.virt.md)
- [11.9.6 Multiple access \[class.paths\]](class.paths.md)
- [11.9.7 Nested classes \[class.access.nest\]](class.access.nest.md)

[^1]: Access permissions are thus transitive and cumulative to nested and local classes.
