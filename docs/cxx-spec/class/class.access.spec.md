# 11.9.1 Access specifiers [class.access.spec]

[Contents](../README.md) · up: [11.9 Member access control \[class.access\]](class.access.md) · prev: [11.9 Member access control \[class.access\]](class.access.md) · next: [11.9.2 Accessibility of base classes and base class members \[class.access.base\]](class.access.base.md)

**1** Member declarations can be labeled by an *access-specifier* ([[class.derived]](class.derived.md)):

```bnf
access-specifier ':' member-specificationₒₚₜ
```

An *access-specifier* specifies the access rules for members following it until the end of the class or until another *access-specifier* is encountered. [*Example 1*:

```cpp
class X {
  int a;            // X::a is private by default: class used
public:
  int b;            // X::b is public
  int c;            // X::c is public
};
```

— *end example*]

**2** Any number of access specifiers is allowed and no particular order is required. [*Example 2*:

```cpp
struct S {
  int a;            // S::a is public by default: struct used
protected:
  int b;            // S::b is protected
private:
  int c;            // S::c is private
public:
  int d;            // S::d is public
};
```

— *end example*]

**3** [*Note 1*: The effect of access control on the order of allocation of data members is specified in [[expr.rel]](../expr/expr.rel.md). — *end note*]

**4** When a member is redeclared within its class definition, the access specified at its redeclaration shall be the same as at its initial declaration. [*Example 3*:

```cpp
struct S {
  class A;
  enum E : int;
private:
  class A { };                  // error: cannot change access
  enum E: int { e0 };           // error: cannot change access
};
```

— *end example*]

**5** [*Note 2*: In a derived class, the lookup of a base class name will find the injected-class-name instead of the name of the base class in the scope in which it was declared. The injected-class-name might be less accessible than the name of the base class in the scope in which it was declared. — *end note*]

[*Example 4*:

```cpp
class A { };
class B : private A { };
class C : public B {
  A* p;             // error: injected-class-name A is inaccessible
  ::A* q;           // OK
};
```

— *end example*]
