# D.7 Non-local use of TU-local entities [depr.local]

[Contents](README.md) · up: [Annex D Compatibility features \[depr\]](depr.md) · prev: [D.6 Redeclaration of `static constexpr` data members \[depr.static.constexpr\]](depr.static.constexpr.md) · next: [D.8 Implicit declaration of copy functions \[depr.impldec\]](depr.impldec.md)

**1** A declaration of a non-TU-local entity that is an exposure ([[basic.link]](basic.link.md)) is deprecated. [*Note 1*: Such a declaration in an importable module unit is ill-formed. — *end note*] [*Example 1*:

```cpp
namespace {
  struct A {
    void f() {}
  };
}
A h();                          // deprecated: not internal linkage
inline void g() {A().f();}      // deprecated: inline and not internal linkage
```

— *end example*]
