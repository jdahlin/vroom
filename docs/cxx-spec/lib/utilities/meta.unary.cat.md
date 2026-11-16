# 20.15.4.1 Primary type categories [meta.unary.cat]

[Contents](../../README.md) · up: [20.15.4 Unary type traits \[meta.unary\]](meta.unary.md) · prev: [20.15.4 Unary type traits \[meta.unary\]](meta.unary.md) · next: [20.15.4.2 Composite type traits \[meta.unary.comp\]](meta.unary.comp.md)

**1** The primary type categories correspond to the descriptions given in subclause [[basic.types]](../../basic/basic.types.md) of the C++ standard.

**2** For any given type `T`, the result of applying one of these templates to `T` and to *cv* `T` shall yield the same result.

**3** [*Note 1*: For any given type `T`, exactly one of the primary type categories has a `value` member that evaluates to `true`. — *end note*]

<a id="tab:meta.unary.cat"></a>
**Table 47: Primary type category predicates** [tab:meta.unary.cat]

| Template | Condition | Comments |
| --- | --- | --- |
| `template<class T>`<br>`struct is_void;` | `T` is `void` |  |
| `template<class T>`<br>`struct is_null_pointer;` | `T` is `nullptr_t` ([[basic.fundamental]](../../basic/basic.fundamental.md)) |  |
| `template<class T>`<br>`struct is_integral;` | `T` is an integral type ([[basic.fundamental]](../../basic/basic.fundamental.md)) |  |
| `template<class T>`<br>`struct is_floating_point;` | `T` is a floating-point type ([[basic.fundamental]](../../basic/basic.fundamental.md)) |  |
| `template<class T>`<br>`struct is_array;` | `T` is an array type ([[basic.compound]](../../basic/basic.compound.md)) of known or unknown extent | Class template `array` ([[array]](../containers/array.md)) is not an array type. |
| `template<class T>`<br>`struct is_pointer;` | `T` is a pointer type ([[basic.compound]](../../basic/basic.compound.md)) | Includes pointers to functions but not pointers to non-static members. |
| `template<class T>`<br>`struct is_lvalue_reference;` | `T` is an lvalue reference type ([[dcl.ref]](../../dcl/dcl.ref.md)) |  |
| `template<class T>`<br>`struct is_rvalue_reference;` | `T` is an rvalue reference type ([[dcl.ref]](../../dcl/dcl.ref.md)) |  |
| `template<class T>`<br>`struct is_member_object_pointer;` | `T` is a pointer to data member |  |
| `template<class T>`<br>`struct is_member_function_pointer;` | `T` is a pointer to member function |  |
| `template<class T>`<br>`struct is_enum;` | `T` is an enumeration type ([[basic.compound]](../../basic/basic.compound.md)) |  |
| `template<class T>`<br>`struct is_union;` | `T` is a union type ([[basic.compound]](../../basic/basic.compound.md)) |  |
| `template<class T>`<br>`struct is_class;` | `T` is a non-union class type ([[basic.compound]](../../basic/basic.compound.md)) |  |
| `template<class T>`<br>`struct is_function;` | `T` is a function type ([[basic.compound]](../../basic/basic.compound.md)) |  |
