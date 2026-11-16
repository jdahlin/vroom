# 7.6.1.7 Type identification [expr.typeid]

[Contents](../README.md) · up: [7.6.1 Postfix expressions \[expr.post\]](expr.post.md) · prev: [7.6.1.6 Dynamic cast \[expr.dynamic.cast\]](expr.dynamic.cast.md) · next: [7.6.1.8 Static cast \[expr.static.cast\]](expr.static.cast.md)

**1** The result of a `typeid` expression is an lvalue of static type `const` `std::type_info` ([[type.info]](../lib/support/type.info.md)) and dynamic type `const` `std::type_info` or `const` *name* where *name* is an implementation-defined class publicly derived from `std::type_info` which preserves the behavior described in [[type.info]](../lib/support/type.info.md).[^1] The lifetime of the object referred to by the lvalue extends to the end of the program. Whether or not the destructor is called for the `std::type_info` object at the end of the program is unspecified.

**2** When `typeid` is applied to a glvalue whose type is a polymorphic class type ([[class.virtual]](../class/class.virtual.md)), the result refers to a `std::type_info` object representing the type of the most derived object ([[intro.object]](../basic/intro.object.md)) (that is, the dynamic type) to which the glvalue refers. If the glvalue is obtained by applying the unary `*` operator to a pointer[^2] and the pointer is a null pointer value ([[basic.compound]](../basic/basic.compound.md)), the `typeid` expression throws an exception ([[except.throw]](../except/except.throw.md)) of a type that would match a handler of type `std::bad_typeid` exception ([[bad.typeid]](../lib/support/bad.typeid.md)).

**3** When `typeid` is applied to an expression other than a glvalue of a polymorphic class type, the result refers to a `std::type_info` object representing the static type of the expression. Lvalue-to-rvalue ([[conv.lval]](conv.lval.md)), array-to-pointer ([[conv.array]](conv.array.md)), and function-to-pointer ([[conv.func]](conv.func.md)) conversions are not applied to the expression. If the expression is a prvalue, the temporary materialization conversion ([[conv.rval]](conv.rval.md)) is applied. The expression is an unevaluated operand ([[expr.prop]](expr.prop.md)).

**4** When `typeid` is applied to a *type-id*, the result refers to a `std::type_info` object representing the type of the *type-id*. If the type of the *type-id* is a reference to a possibly cv-qualified type, the result of the `typeid` expression refers to a `std::type_info` object representing the cv-unqualified referenced type. If the type of the *type-id* is a class type or a reference to a class type, the class shall be completely-defined. [*Note 1*: The *type-id* cannot denote a function type with a *cv-qualifier-seq* or a *ref-qualifier* ([[dcl.fct]](../dcl/dcl.fct.md)). — *end note*]

**5** If the type of the expression or *type-id* is a cv-qualified type, the result of the `typeid` expression refers to a `std::type_info` object representing the cv-unqualified type. [*Example 1*:

```cpp
class D { /* ... */ };
D d1;
const D d2;

typeid(d1) == typeid(d2);       // yields true
typeid(D)  == typeid(const D);  // yields true
typeid(D)  == typeid(d2);       // yields true
typeid(D)  == typeid(const D&); // yields true
```

— *end example*]

**6** If the header `<typeinfo>` ([[type.info]](../lib/support/type.info.md)) is not imported or included prior to a use of `typeid`, the program is ill-formed.

**7** [*Note 2*: Subclause [[class.cdtor]](../class/class.cdtor.md) describes the behavior of `typeid` applied to an object under construction or destruction. — *end note*]

[^1]: The recommended name for such a class is `extended_type_info`.
[^2]: If `p` is an expression of pointer type, then `*p`, `(*p)`, `*(p)`, `((*p))`, `*((p))`, and so on all meet this requirement.
