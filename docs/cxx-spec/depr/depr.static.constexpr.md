# D.6 Redeclaration of `static constexpr` data members [depr.static.constexpr]

[Contents](../README.md) · up: [Annex D Compatibility features \[depr\]](depr.md) · prev: [D.5 Deprecated `volatile` types \[depr.volatile.type\]](depr.volatile.type.md) · next: [D.7 Non-local use of TU-local entities \[depr.local\]](depr.local.md)

**1** For compatibility with prior C++ International Standards, a `constexpr` static data member may be redundantly redeclared outside the class with no initializer. This usage is deprecated. [*Example 1*:

```cpp
struct A {
  static constexpr int n = 5;   // definition (declaration in C++ 2014)
};

constexpr int A::n;             // redundant declaration (definition in C++ 2014)
```

— *end example*]
