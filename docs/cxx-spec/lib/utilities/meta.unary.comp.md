# 20.15.4.2 Composite type traits [meta.unary.comp]

[Contents](../../README.md) · up: [20.15.4 Unary type traits \[meta.unary\]](meta.unary.md) · prev: [20.15.4.1 Primary type categories \[meta.unary.cat\]](meta.unary.cat.md) · next: [20.15.4.3 Type properties \[meta.unary.prop\]](meta.unary.prop.md)

**1** These templates provide convenient compositions of the primary type categories, corresponding to the descriptions given in subclause [[basic.types]](../../basic/basic.types.md).

**2** For any given type `T`, the result of applying one of these templates to `T` and to *cv* `T` shall yield the same result.

<a id="tab:meta.unary.comp"></a>
**Table 48: Composite type category predicates** [tab:meta.unary.comp]

| Template | Condition | Comments |
| --- | --- | --- |
| `template<class T>`<br>`struct is_reference;` | `T` is an lvalue reference or an rvalue reference |  |
| `template<class T>`<br>`struct is_arithmetic;` | `T` is an arithmetic type ([[basic.fundamental]](../../basic/basic.fundamental.md)) |  |
| `template<class T>`<br>`struct is_fundamental;` | `T` is a fundamental type ([[basic.fundamental]](../../basic/basic.fundamental.md)) |  |
| `template<class T>`<br>`struct is_object;` | `T` is an object type ([[basic.types]](../../basic/basic.types.md)) |  |
| `template<class T>`<br>`struct is_scalar;` | `T` is a scalar type ([[basic.types]](../../basic/basic.types.md)) |  |
| `template<class T>`<br>`struct is_compound;` | `T` is a compound type ([[basic.compound]](../../basic/basic.compound.md)) |  |
| `template<class T>`<br>`struct is_member_pointer;` | `T` is a pointer-to-member type ([[basic.compound]](../../basic/basic.compound.md)) |  |
