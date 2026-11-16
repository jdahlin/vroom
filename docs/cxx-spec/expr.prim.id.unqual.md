# 7.5.4.1 Unqualified names [expr.prim.id.unqual]

[Contents](README.md) · up: [7.5.4 Names \[expr.prim.id\]](expr.prim.id.md) · prev: [7.5.4 Names \[expr.prim.id\]](expr.prim.id.md) · next: [7.5.4.2 Qualified names \[expr.prim.id.qual\]](expr.prim.id.qual.md)

```bnf
unqualified-id:
    identifier
    operator-function-id
    conversion-function-id
    literal-operator-id
    '~' type-name
    '~' decltype-specifier
    template-id
```

**1** An *identifier* is only an *id-expression* if it has been suitably declared ([[dcl.dcl]](dcl.dcl.md)) or if it appears as part of a *declarator-id* ([[dcl.decl]](dcl.decl.md)). An *identifier* that names a coroutine parameter refers to the copy of the parameter ([[dcl.fct.def.coroutine]](dcl.fct.def.coroutine.md)). [*Note 1*: For *operator-function-id*s, see [[over.oper]](over.oper.md); for *conversion-function-id*s, see [[class.conv.fct]](class.conv.fct.md); for *literal-operator-id*s, see [[over.literal]](over.literal.md); for *template-id*s, see [[temp.names]](temp.names.md). A *type-name* or *decltype-specifier* prefixed by `~` denotes the destructor of the type so named; see [[expr.prim.id.dtor]](expr.prim.id.dtor.md). Within the definition of a non-static member function, an *identifier* that names a non-static member is transformed to a class member access expression ([[class.mfct.non-static]](class.mfct.non-static.md)). — *end note*]

**2** The result is the entity denoted by the identifier. If the entity is a local entity and naming it from outside of an unevaluated operand within the declarative region where the *unqualified-id* appears would result in some intervening *lambda-expression* capturing it by copy ([[expr.prim.lambda.capture]](expr.prim.lambda.capture.md)), the type of the expression is the type of a class member access expression ([[expr.ref]](expr.ref.md)) naming the non-static data member that would be declared for such a capture in the closure object of the innermost such intervening *lambda-expression*. [*Note 2*: If that *lambda-expression* is not declared `mutable`, the type of such an identifier will typically be `const` qualified. — *end note*] The type of the expression is the type of the result. [*Note 3*: If the entity is a template parameter object for a template parameter of type `T` ([[temp.param]](temp.param.md)), the type of the expression is `const T`. — *end note*] [*Note 4*: The type will be adjusted as described in [[expr.type]](expr.type.md) if it is cv-qualified or is a reference type. — *end note*] The expression is an lvalue if the entity is a function, variable, structured binding ([[dcl.struct.bind]](dcl.struct.bind.md)), data member, or template parameter object and a prvalue otherwise ([[basic.lval]](basic.lval.md)); it is a bit-field if the identifier designates a bit-field. [*Example 1*:

```cpp
void f() {
  float x, &r = x;
  [=] {
    decltype(x) y1;             // y1 has type float
    decltype((x)) y2 = y1;      // y2 has type float const& because this lambda
                                // is not mutable and x is an lvalue
    decltype(r) r1 = y1;        // r1 has type float&
    decltype((r)) r2 = y2;      // r2 has type float const&
  };
}
```

— *end example*]
