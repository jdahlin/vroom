# 7.5 Primary expressions [expr.prim]

[Contents](../README.md) · up: [7 Expressions \[expr\]](expr.md) · prev: [7.4 Usual arithmetic conversions \[expr.arith.conv\]](expr.arith.conv.md) · next: [7.5.1 Literals \[expr.prim.literal\]](expr.prim.literal.md)

```bnf
primary-expression:
    literal
    'this'
    '(' expression ')'
    id-expression
    lambda-expression
    fold-expression
    requires-expression
```

## Subclauses

- [7.5.1 Literals \[expr.prim.literal\]](expr.prim.literal.md)
- [7.5.2 This \[expr.prim.this\]](expr.prim.this.md)
- [7.5.3 Parentheses \[expr.prim.paren\]](expr.prim.paren.md)
- [7.5.4 Names \[expr.prim.id\]](expr.prim.id.md)
  - [7.5.4.1 Unqualified names \[expr.prim.id.unqual\]](expr.prim.id.unqual.md)
  - [7.5.4.2 Qualified names \[expr.prim.id.qual\]](expr.prim.id.qual.md)
  - [7.5.4.3 Destruction \[expr.prim.id.dtor\]](expr.prim.id.dtor.md)
- [7.5.5 Lambda expressions \[expr.prim.lambda\]](expr.prim.lambda.md)
  - [7.5.5.1 Closure types \[expr.prim.lambda.closure\]](expr.prim.lambda.closure.md)
  - [7.5.5.2 Captures \[expr.prim.lambda.capture\]](expr.prim.lambda.capture.md)
- [7.5.6 Fold expressions \[expr.prim.fold\]](expr.prim.fold.md)
- [7.5.7 Requires expressions \[expr.prim.req\]](expr.prim.req.md)
  - [7.5.7.1 Simple requirements \[expr.prim.req.simple\]](expr.prim.req.simple.md)
  - [7.5.7.2 Type requirements \[expr.prim.req.type\]](expr.prim.req.type.md)
  - [7.5.7.3 Compound requirements \[expr.prim.req.compound\]](expr.prim.req.compound.md)
  - [7.5.7.4 Nested requirements \[expr.prim.req.nested\]](expr.prim.req.nested.md)
