# 11.9.7 Nested classes [class.access.nest]

[Contents](README.md) · up: [11.9 Member access control \[class.access\]](class.access.md) · prev: [11.9.6 Multiple access \[class.paths\]](class.paths.md) · next: [11.10 Initialization \[class.init\]](class.init.md)

**1** A nested class is a member and as such has the same access rights as any other member. The members of an enclosing class have no special access to members of a nested class; the usual access rules ([[class.access]](class.access.md)) shall be obeyed. [*Example 1*:

```cpp
class E {
  int x;
  class B { };

  class I {
    B b;                        // OK: E::I can access E::B
    int y;
    void f(E* p, int i) {
      p->x = i;                 // OK: E::I can access E::x
    }
  };

  int g(I* p) {
    return p->y;                // error: I::y is private
  }
};
```

— *end example*]
