# 7.5.4 Names [expr.prim.id]

[Contents](README.md) · up: [7.5 Primary expressions \[expr.prim\]](expr.prim.md) · prev: [7.5.3 Parentheses \[expr.prim.paren\]](expr.prim.paren.md) · next: [7.5.4.1 Unqualified names \[expr.prim.id.unqual\]](expr.prim.id.unqual.md)

```bnf
id-expression:
    unqualified-id
    qualified-id
```

**1** An *id-expression* is a restricted form of a *primary-expression*. [*Note 1*: An *id-expression* can appear after `.` and `->` operators ([[expr.ref]](expr.ref.md)). — *end note*]

**2** An *id-expression* that denotes a non-static data member or non-static member function of a class can only be used:

- as part of a class member access ([[expr.ref]](expr.ref.md)) in which the object expression refers to the member’s class[^1] or a class derived from that class, or

- to form a pointer to member ([[expr.unary.op]](expr.unary.op.md)), or

- if that *id-expression* denotes a non-static data member and it appears in an unevaluated operand. [*Example 1*:

  ```cpp
  struct S {
    int m;
  };
  int i = sizeof(S::m);           // OK
  int j = sizeof(S::m + 42);      // OK
  ```

  — *end example*]

**3** A potentially-evaluated *id-expression* that denotes an immediate function ([[dcl.constexpr]](dcl.constexpr.md)) shall appear only

- as a subexpression of an immediate invocation, or
- in an immediate function context ([[expr.const]](expr.const.md)).

**4** For an *id-expression* that denotes an overload set, overload resolution is performed to select a unique function ([[over.match]](over.match.md), [[over.over]](over.over.md)). [*Note 2*: A program cannot refer to a function with a trailing *requires-clause* whose *constraint-expression* is not satisfied, because such functions are never selected by overload resolution. [*Example 2*:

```cpp
template<typename T> struct A {
  static void f(int) requires false;
}

void g() {
  A<int>::f(0);                         // error: cannot call f
  void (*p1)(int) = A<int>::f;          // error: cannot take the address of f
  decltype(A<int>::f)* p2 = nullptr;    // error: the type decltype(A<int>::f) is invalid
}
```

In each case, the constraints of `f` are not satisfied. In the declaration of `p2`, those constraints are required to be satisfied even though `f` is an unevaluated operand ([[expr.prop]](expr.prop.md)). — *end example*] — *end note*]

## Subclauses

- [7.5.4.1 Unqualified names \[expr.prim.id.unqual\]](expr.prim.id.unqual.md)
- [7.5.4.2 Qualified names \[expr.prim.id.qual\]](expr.prim.id.qual.md)
- [7.5.4.3 Destruction \[expr.prim.id.dtor\]](expr.prim.id.dtor.md)

[^1]: This also applies when the object expression is an implicit `(*this)` ([[class.mfct.non-static]](class.mfct.non-static.md)).
