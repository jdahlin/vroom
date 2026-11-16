# 20.15 Metaprogramming and type traits [meta]

[Contents](../README.md) · up: [20 General utilities library \[utilities\]](utilities.md) · prev: [20.14.18 Class template `hash` \[unord.hash\]](unord.hash.md) · next: [20.15.1 Requirements \[meta.rqmts\]](meta.rqmts.md)

**1** This subclause describes components used by C++ programs, particularly in templates, to support the widest possible range of types, optimise template code usage, detect type related user errors, and perform type inference and transformation at compile time. It includes type classification traits, type property inspection traits, and type transformations. The type classification traits describe a complete taxonomy of all possible C++ types, and state where in that taxonomy a given type belongs. The type property inspection traits allow important characteristics of types or of combinations of types to be inspected. The type transformations allow certain properties of types to be manipulated.

**2** All functions specified in this subclause are signal-safe ([[support.signal]](support.signal.md)).

## Subclauses

- [20.15.1 Requirements \[meta.rqmts\]](meta.rqmts.md)
- [20.15.2 Header `<type_traits>` synopsis \[meta.type.synop\]](meta.type.synop.md)
- [20.15.3 Helper classes \[meta.help\]](meta.help.md)
- [20.15.4 Unary type traits \[meta.unary\]](meta.unary.md)
  - [20.15.4.1 Primary type categories \[meta.unary.cat\]](meta.unary.cat.md)
  - [20.15.4.2 Composite type traits \[meta.unary.comp\]](meta.unary.comp.md)
  - [20.15.4.3 Type properties \[meta.unary.prop\]](meta.unary.prop.md)
- [20.15.5 Type property queries \[meta.unary.prop.query\]](meta.unary.prop.query.md)
- [20.15.6 Relationships between types \[meta.rel\]](meta.rel.md)
- [20.15.7 Transformations between types \[meta.trans\]](meta.trans.md)
  - [20.15.7.1 Const-volatile modifications \[meta.trans.cv\]](meta.trans.cv.md)
  - [20.15.7.2 Reference modifications \[meta.trans.ref\]](meta.trans.ref.md)
  - [20.15.7.3 Sign modifications \[meta.trans.sign\]](meta.trans.sign.md)
  - [20.15.7.4 Array modifications \[meta.trans.arr\]](meta.trans.arr.md)
  - [20.15.7.5 Pointer modifications \[meta.trans.ptr\]](meta.trans.ptr.md)
  - [20.15.7.6 Other transformations \[meta.trans.other\]](meta.trans.other.md)
- [20.15.8 Logical operator traits \[meta.logical\]](meta.logical.md)
- [20.15.9 Member relationships \[meta.member\]](meta.member.md)
- [20.15.10 Constant evaluation context \[meta.const.eval\]](meta.const.eval.md)
