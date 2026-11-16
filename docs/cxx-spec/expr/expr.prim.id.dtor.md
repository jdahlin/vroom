# 7.5.4.3 Destruction [expr.prim.id.dtor]

[Contents](../README.md) · up: [7.5.4 Names \[expr.prim.id\]](expr.prim.id.md) · prev: [7.5.4.2 Qualified names \[expr.prim.id.qual\]](expr.prim.id.qual.md) · next: [7.5.5 Lambda expressions \[expr.prim.lambda\]](expr.prim.lambda.md)

**1** An *id-expression* that denotes the destructor of a type `T` names the destructor of `T` if `T` is a class type ([[class.dtor]](../class/class.dtor.md)), otherwise the *id-expression* is said to name a *pseudo-destructor*.

**2** If the *id-expression* names a pseudo-destructor, `T` shall be a scalar type and the *id-expression* shall appear as the right operand of a class member access ([[expr.ref]](expr.ref.md)) that forms the *postfix-expression* of a function call ([[expr.call]](expr.call.md)). [*Note 1*: Such a call ends the lifetime of the object ([[expr.call]](expr.call.md), [[basic.life]](../basic/basic.life.md)). — *end note*]

**3** [*Example 1*:

```cpp
struct C { };
void f() {
  C * pc = new C;
  using C2 = C;
  pc->C::~C2();     // OK, destroys *pc
  C().C::~C();      // undefined behavior: temporary of type C destroyed twice
  using T = int;
  0 .T::~T();       // OK, no effect
  0.T::~T();        // error: 0.T is a user-defined-floating-point-literal ([lex.ext])
}
```

— *end example*]
