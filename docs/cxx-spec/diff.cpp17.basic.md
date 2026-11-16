# C.1.2 [[basic]](basic.md): basics [diff.cpp17.basic]

[Contents](README.md) · up: [C.1 C++ and ISO C++ 2017 \[diff.cpp17\]](diff.cpp17.md) · prev: [C.1.1 [[lex]](lex.md): lexical conventions \[diff.cpp17.lex\]](diff.cpp17.lex.md) · next: [C.1.3 [[expr]](expr.md): expressions \[diff.cpp17.expr\]](diff.cpp17.expr.md)

**1** **Affected subclause:** [[basic.life]](basic.life.md) <br>**Change:** A pseudo-destructor call ends the lifetime of the object to which it is applied. <br>**Rationale:** Increase consistency of the language model. <br>**Effect on original feature:** Valid ISO C++ 2017 code may be ill-formed or have undefined behavior in this International Standard. [*Example 1*:

```cpp
int f() {
  int a = 123;
  using T = int;
  a.~T();
  return a;         // undefined behavior; previously returned 123
}
```

— *end example*]

**2** **Affected subclause:** [[intro.races]](intro.races.md) <br>**Change:** Except for the initial release operation, a release sequence consists solely of atomic read-modify-write operations. <br>**Rationale:** Removal of rarely used and confusing feature. <br>**Effect on original feature:** If a `memory_order_release` atomic store is followed by a `memory_order_relaxed` store to the same variable by the same thread, then reading the latter value with a `memory_order_acquire` load no longer provides any “happens before” guarantees, even in the absence of intervening stores by another thread.
