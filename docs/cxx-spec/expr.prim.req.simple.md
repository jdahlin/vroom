# 7.5.7.1 Simple requirements [expr.prim.req.simple]

[Contents](README.md) · up: [7.5.7 Requires expressions \[expr.prim.req\]](expr.prim.req.md) · prev: [7.5.7 Requires expressions \[expr.prim.req\]](expr.prim.req.md) · next: [7.5.7.2 Type requirements \[expr.prim.req.type\]](expr.prim.req.type.md)

```bnf
simple-requirement:
    expression ';'
```

**1** A *simple-requirement* asserts the validity of an *expression*. [*Note 1*: The enclosing *requires-expression* will evaluate to `false` if substitution of template arguments into the *expression* fails. The *expression* is an unevaluated operand ([[expr.prop]](expr.prop.md)). — *end note*] [*Example 1*:

```cpp
template<typename T> concept C =
  requires (T a, T b) {
    a + b;          // C<T> is true if a + b is a valid expression
  };
```

— *end example*]

**2** A *requirement* that starts with a `requires` token is never interpreted as a *simple-requirement*. [*Note 2*: This simplifies distinguishing between a *simple-requirement* and a *nested-requirement*. — *end note*]
