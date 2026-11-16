# 7.6.1.9 Reinterpret cast [expr.reinterpret.cast]

[Contents](README.md) · up: [7.6.1 Postfix expressions \[expr.post\]](expr.post.md) · prev: [7.6.1.8 Static cast \[expr.static.cast\]](expr.static.cast.md) · next: [7.6.1.10 Const cast \[expr.const.cast\]](expr.const.cast.md)

**1** The result of the expression `reinterpret_cast<T>(v)` is the result of converting the expression `v` to type `T`. If `T` is an lvalue reference type or an rvalue reference to function type, the result is an lvalue; if `T` is an rvalue reference to object type, the result is an xvalue; otherwise, the result is a prvalue and the lvalue-to-rvalue ([[conv.lval]](conv.lval.md)), array-to-pointer ([[conv.array]](conv.array.md)), and function-to-pointer ([[conv.func]](conv.func.md)) standard conversions are performed on the expression `v`. Conversions that can be performed explicitly using `reinterpret_cast` are listed below. No other conversion can be performed explicitly using `reinterpret_cast`.

**2** The `reinterpret_cast` operator shall not cast away constness ([[expr.const.cast]](expr.const.cast.md)). An expression of integral, enumeration, pointer, or pointer-to-member type can be explicitly converted to its own type; such a cast yields the value of its operand.

**3** [*Note 1*: The mapping performed by `reinterpret_cast` might, or might not, produce a representation different from the original value. — *end note*]

**4** A pointer can be explicitly converted to any integral type large enough to hold all values of its type. The mapping function is implementation-defined. [*Note 2*: It is intended to be unsurprising to those who know the addressing structure of the underlying machine. — *end note*] A value of type `std::nullptr_t` can be converted to an integral type; the conversion has the same meaning and validity as a conversion of `(void*)0` to the integral type. [*Note 3*: A `reinterpret_cast` cannot be used to convert a value of any type to the type `std::nullptr_t`. — *end note*]

**5** A value of integral type or enumeration type can be explicitly converted to a pointer. A pointer converted to an integer of sufficient size (if any such exists on the implementation) and back to the same pointer type will have its original value; mappings between pointers and integers are otherwise implementation-defined. [*Note 4*: Except as described in [[basic.stc.dynamic.safety]](basic.stc.dynamic.safety.md), the result of such a conversion will not be a safely-derived pointer value. — *end note*]

**6** A function pointer can be explicitly converted to a function pointer of a different type. [*Note 5*: The effect of calling a function through a pointer to a function type ([[dcl.fct]](dcl.fct.md)) that is not the same as the type used in the definition of the function is undefined ([[expr.call]](expr.call.md)). — *end note*] Except that converting a prvalue of type “pointer to `T1`” to the type “pointer to `T2`” (where `T1` and `T2` are function types) and back to its original type yields the original pointer value, the result of such a pointer conversion is unspecified. [*Note 6*: See also [[conv.ptr]](conv.ptr.md) for more details of pointer conversions. — *end note*]

**7** An object pointer can be explicitly converted to an object pointer of a different type.[^1] When a prvalue `v` of object pointer type is converted to the object pointer type “pointer to *cv* `T`”, the result is `static_cast<cv T*>(static_cast<cv void*>(v))`. [*Note 7*: Converting a prvalue of type “pointer to `T1`” to the type “pointer to `T2`” (where `T1` and `T2` are object types and where the alignment requirements of `T2` are no stricter than those of `T1`) and back to its original type yields the original pointer value. — *end note*]

**8** Converting a function pointer to an object pointer type or vice versa is conditionally-supported. The meaning of such a conversion is implementation-defined, except that if an implementation supports conversions in both directions, converting a prvalue of one type to the other type and back, possibly with different cv-qualification, shall yield the original pointer value.

**9** The null pointer value ([[basic.compound]](basic.compound.md)) is converted to the null pointer value of the destination type. [*Note 8*: A null pointer constant of type `std::nullptr_t` cannot be converted to a pointer type, and a null pointer constant of integral type is not necessarily converted to a null pointer value. — *end note*]

**10** A prvalue of type “pointer to member of `X` of type `T1`” can be explicitly converted to a prvalue of a different type “pointer to member of `Y` of type `T2`” if `T1` and `T2` are both function types or both object types.[^2] The null member pointer value ([[conv.mem]](conv.mem.md)) is converted to the null member pointer value of the destination type. The result of this conversion is unspecified, except in the following cases:

- Converting a prvalue of type “pointer to member function” to a different pointer-to-member-function type and back to its original type yields the original pointer-to-member value.
- Converting a prvalue of type “pointer to data member of `X` of type `T1`” to the type “pointer to data member of `Y` of type `T2`” (where the alignment requirements of `T2` are no stricter than those of `T1`) and back to its original type yields the original pointer-to-member value.

**11** A glvalue of type `T1`, designating an object *x*, can be cast to the type “reference to `T2`” if an expression of type “pointer to `T1`” can be explicitly converted to the type “pointer to `T2`” using a `reinterpret_cast`. The result is that of `*reinterpret_cast<T2 *>(p)` where `p` is a pointer to *x* of type “pointer to `T1`”. No temporary is created, no copy is made, and no constructors ([[class.ctor]](class.ctor.md)) or conversion functions ([[class.conv]](class.conv.md)) are called.[^3]

[^1]: The types may have different *cv*-qualifiers, subject to the overall restriction that a `reinterpret_cast` cannot cast away constness.
[^2]: `T1` and `T2` may have different *cv*-qualifiers, subject to the overall restriction that a `reinterpret_cast` cannot cast away constness.
[^3]: This is sometimes referred to as a *type pun* when the result refers to the same object as the source glvalue.
