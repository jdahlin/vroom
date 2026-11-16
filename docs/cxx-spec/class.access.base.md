# 11.9.2 Accessibility of base classes and base class members [class.access.base]

[Contents](README.md) · up: [11.9 Member access control \[class.access\]](class.access.md) · prev: [11.9.1 Access specifiers \[class.access.spec\]](class.access.spec.md) · next: [11.9.3 Friends \[class.friend\]](class.friend.md)

**1** If a class is declared to be a base class ([[class.derived]](class.derived.md)) for another class using the `public` access specifier, the public members of the base class are accessible as public members of the derived class and protected members of the base class are accessible as protected members of the derived class. If a class is declared to be a base class for another class using the `protected` access specifier, the public and protected members of the base class are accessible as protected members of the derived class. If a class is declared to be a base class for another class using the `private` access specifier, the public and protected members of the base class are accessible as private members of the derived class.[^1]

**2** In the absence of an *access-specifier* for a base class, `public` is assumed when the derived class is defined with the *class-key* `struct` and `private` is assumed when the class is defined with the *class-key* `class`. [*Example 1*:

```cpp
class B { /* ... */ };
class D1 : private B { /* ... */ };
class D2 : public B { /* ... */ };
class D3 : B { /* ... */ };             // B private by default
struct D4 : public B { /* ... */ };
struct D5 : private B { /* ... */ };
struct D6 : B { /* ... */ };            // B public by default
class D7 : protected B { /* ... */ };
struct D8 : protected B { /* ... */ };
```

Here `B` is a public base of `D2`, `D4`, and `D6`, a private base of `D1`, `D3`, and `D5`, and a protected base of `D7` and `D8`. — *end example*]

**3** [*Note 1*: A member of a private base class might be inaccessible as an inherited member name, but accessible directly. Because of the rules on pointer conversions ([[conv.ptr]](conv.ptr.md)) and explicit casts ([[expr.type.conv]](expr.type.conv.md), [[expr.static.cast]](expr.static.cast.md), [[expr.cast]](expr.cast.md)), a conversion from a pointer to a derived class to a pointer to an inaccessible base class might be ill-formed if an implicit conversion is used, but well-formed if an explicit cast is used. For example,

```cpp
class B {
public:
  int mi;                       // non-static member
  static int si;                // static member
};
class D : private B {
};
class DD : public D {
  void f();
};

void DD::f() {
  mi = 3;                       // error: mi is private in D
  si = 3;                       // error: si is private in D
  ::B  b;
  b.mi = 3;                     // OK (b.mi is different from this->mi)
  b.si = 3;                     // OK (b.si is different from this->si)
  ::B::si = 3;                  // OK
  ::B* bp1 = this;              // error: B is a private base class
  ::B* bp2 = (::B*)this;        // OK with cast
  bp2->mi = 3;                  // OK: access through a pointer to B.
}
```

— *end note*]

**4** A base class `B` of `N` is *accessible* at *R*, if

- an invented public member of `B` would be a public member of `N`, or
- *R* occurs in a member or friend of class `N`, and an invented public member of `B` would be a private or protected member of `N`, or
- *R* occurs in a member or friend of a class `P` derived from `N`, and an invented public member of `B` would be a private or protected member of `P`, or
- there exists a class `S` such that `B` is a base class of `S` accessible at *R* and `S` is a base class of `N` accessible at *R*.

[*Example 2*:

```cpp
class B {
public:
  int m;
};

class S: private B {
  friend class N;
};

class N: private S {
  void f() {
    B* p = this;    // OK because class S satisfies the fourth condition above: B is a base class of N
                    // accessible in f() because B is an accessible base class of S and S is an accessible
                    // base class of N.
  }
};
```

— *end example*]

**5** If a base class is accessible, one can implicitly convert a pointer to a derived class to a pointer to that base class ([[conv.ptr]](conv.ptr.md), [[conv.mem]](conv.mem.md)). [*Note 2*: It follows that members and friends of a class `X` can implicitly convert an `X*` to a pointer to a private or protected immediate base class of `X`. — *end note*] The access to a member is affected by the class in which the member is named. This naming class is the class in which the member name was looked up and found. [*Note 3*: This class can be explicit, e.g., when a *qualified-id* is used, or implicit, e.g., when a class member access operator ([[expr.ref]](expr.ref.md)) is used (including cases where an implicit “`this->`” is added). If both a class member access operator and a *qualified-id* are used to name the member (as in `p->T::m`), the class naming the member is the class denoted by the *nested-name-specifier* of the *qualified-id* (that is, `T`). — *end note*] A member `m` is accessible at the point *R* when named in class `N` if

- `m` as a member of `N` is public, or

- `m` as a member of `N` is private, and *R* occurs in a member or friend of class `N`, or

- `m` as a member of `N` is protected, and *R* occurs in a member or friend of class `N`, or in a member of a class `P` derived from `N`, where `m` as a member of `P` is public, private, or protected, or

- there exists a base class `B` of `N` that is accessible at *R*, and `m` is accessible at *R* when named in class `B`. [*Example 3*:

  ```cpp
  class B;
  class A {
  private:
    int i;
    friend void f(B*);
  };
  class B : public A { };
  void f(B* p) {
    p->i = 1;         // OK: B* can be implicitly converted to A*, and f has access to i in A
  }
  ```

  — *end example*]

**6** If a class member access operator, including an implicit “`this->`”, is used to access a non-static data member or non-static member function, the reference is ill-formed if the left operand (considered as a pointer in the “`.`” operator case) cannot be implicitly converted to a pointer to the naming class of the right operand. [*Note 4*: This requirement is in addition to the requirement that the member be accessible as named. — *end note*]

[^1]: As specified previously in [[class.access]](class.access.md), private members of a base class remain inaccessible even to derived classes unless friend declarations within the base class definition are used to grant access explicitly.
