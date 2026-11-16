# 11.4.7.1 Conversion by constructor [class.conv.ctor]

[Contents](../README.md) · up: [11.4.7 Conversions \[class.conv\]](class.conv.md) · prev: [11.4.7 Conversions \[class.conv\]](class.conv.md) · next: [11.4.7.2 Conversion functions \[class.conv.fct\]](class.conv.fct.md)

**1** A constructor that is not explicit ([[dcl.fct.spec]](../dcl/dcl.fct.spec.md)) specifies a conversion from the types of its parameters (if any) to the type of its class. Such a constructor is called a *converting constructor*. [*Example 1*:

```cpp
struct X {
    X(int);
    X(const char*, int =0);
    X(int, int);
};

void f(X arg) {
  X a = 1;          // a = X(1)
  X b = "Jessie";   // b = X("Jessie",0)
  a = 2;            // a = X(2)
  f(3);             // f(X(3))
  f({1, 2});        // f(X(1,2))
}
```

— *end example*]

**2** [*Note 1*: An explicit constructor constructs objects just like non-explicit constructors, but does so only where the direct-initialization syntax ([[dcl.init]](../dcl/dcl.init.md)) or where casts ([[expr.static.cast]](../expr/expr.static.cast.md), [[expr.cast]](../expr/expr.cast.md)) are explicitly used; see also [[over.match.copy]](../over/over.match.copy.md). A default constructor may be an explicit constructor; such a constructor will be used to perform default-initialization or value-initialization ([[dcl.init]](../dcl/dcl.init.md)). [*Example 2*:

```cpp
struct Z {
  explicit Z();
  explicit Z(int);
  explicit Z(int, int);
};

Z a;                            // OK: default-initialization performed
Z b{};                          // OK: direct initialization syntax used
Z c = {};                       // error: copy-list-initialization
Z a1 = 1;                       // error: no implicit conversion
Z a3 = Z(1);                    // OK: direct initialization syntax used
Z a2(1);                        // OK: direct initialization syntax used
Z* p = new Z(1);                // OK: direct initialization syntax used
Z a4 = (Z)1;                    // OK: explicit cast used
Z a5 = static_cast<Z>(1);       // OK: explicit cast used
Z a6 = { 3, 4 };                // error: no implicit conversion
```

— *end example*] — *end note*]

**3** A non-explicit copy/move constructor ([[class.copy.ctor]](class.copy.ctor.md)) is a converting constructor. [*Note 2*: An implicitly-declared copy/move constructor is not an explicit constructor; it may be called for implicit type conversions. — *end note*]
