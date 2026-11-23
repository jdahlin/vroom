# 7.6.1.8 Static cast [expr.static.cast]

[Contents](../README.md) · up: [7.6.1 Postfix expressions \[expr.post\]](expr.post.md) · prev: [7.6.1.7 Type identification \[expr.typeid\]](expr.typeid.md) · next: [7.6.1.9 Reinterpret cast \[expr.reinterpret.cast\]](expr.reinterpret.cast.md)

**1** The result of the expression `static_cast<T>(v)` is the result of converting the expression `v` to type `T`. If `T` is an lvalue reference type or an rvalue reference to function type, the result is an lvalue; if `T` is an rvalue reference to object type, the result is an xvalue; otherwise, the result is a prvalue. The `static_cast` operator shall not cast away constness ([[expr.const.cast]](expr.const.cast.md)).

**2** An lvalue of type “*cv1* `B`”, where `B` is a class type, can be cast to type “reference to *cv2* `D`”, where `D` is a class derived ([[class.derived]](../class/class.derived.md)) from `B`, if *cv2* is the same cv-qualification as, or greater cv-qualification than, *cv1*. If `B` is a virtual base class of `D` or a base class of a virtual base class of `D`, or if no valid standard conversion from “pointer to `D`” to “pointer to `B`” exists ([[conv.ptr]](conv.ptr.md)), the program is ill-formed. An xvalue of type “*cv1* `B`” can be cast to type “rvalue reference to *cv2* `D`” with the same constraints as for an lvalue of type “*cv1* `B`”. If the object of type “*cv1* `B`” is actually a base class subobject of an object of type `D`, the result refers to the enclosing object of type `D`. Otherwise, the behavior is undefined. [*Example 1*:

```cpp
struct B { };
struct D : public B { };
D d;
B &br = d;

static_cast<D&>(br);            // produces lvalue denoting the original d object
```

— *end example*]

**3** An lvalue of type “*cv1* `T1`” can be cast to type “rvalue reference to *cv2* `T2`” if “*cv2* `T2`” is reference-compatible with “*cv1* `T1`” ([[dcl.init.ref]](../dcl/dcl.init.ref.md)). If the value is not a bit-field, the result refers to the object or the specified base class subobject thereof; otherwise, the lvalue-to-rvalue conversion ([[conv.lval]](conv.lval.md)) is applied to the bit-field and the resulting prvalue is used as the *expression* of the `static_cast` for the remainder of this subclause. If `T2` is an inaccessible ([[class.access]](../class/class.access.md)) or ambiguous ([[class.member.lookup]](../class/class.member.lookup.md)) base class of `T1`, a program that necessitates such a cast is ill-formed.

**4** An expression *E* can be explicitly converted to a type `T` if there is an implicit conversion sequence ([[over.best.ics]](../over/over.best.ics.md)) from *E* to `T`, if overload resolution for a direct-initialization ([[dcl.init]](../dcl/dcl.init.md)) of an object or reference of type `T` from *E* would find at least one viable function ([[over.match.viable]](../over/over.match.viable.md)), or if `T` is an aggregate type ([[dcl.init.aggr]](../dcl/dcl.init.aggr.md)) having a first element `x` and there is an implicit conversion sequence from *E* to the type of `x`. If `T` is a reference type, the effect is the same as performing the declaration and initialization

```cpp
T t(E);
```

for some invented temporary variable `t` ([[dcl.init]](../dcl/dcl.init.md)) and then using the temporary variable as the result of the conversion. Otherwise, the result object is direct-initialized from *E*. [*Note 1*: The conversion is ill-formed when attempting to convert an expression of class type to an inaccessible or ambiguous base class. — *end note*] [*Note 2*: If `T` is “array of unknown bound of `U`”, this direct-initialization defines the type of the expression as `U[1]`. — *end note*]

**5** Otherwise, the `static_cast` shall perform one of the conversions listed below. No other conversion shall be performed explicitly using a `static_cast`.

**6** Any expression can be explicitly converted to type *cv* `void`, in which case it becomes a discarded-value expression ([[expr.prop]](expr.prop.md)). [*Note 3*: However, if the value is in a temporary object ([[class.temporary]](../basic/class.temporary.md)), the destructor for that object is not executed until the usual time, and the value of the object is preserved for the purpose of executing the destructor. — *end note*]

**7** The inverse of any standard conversion sequence ([[conv]](conv.md)) not containing an lvalue-to-rvalue ([[conv.lval]](conv.lval.md)), array-to-pointer ([[conv.array]](conv.array.md)), function-to-pointer ([[conv.func]](conv.func.md)), null pointer ([[conv.ptr]](conv.ptr.md)), null member pointer ([[conv.mem]](conv.mem.md)), boolean ([[conv.bool]](conv.bool.md)), or function pointer ([[conv.fctptr]](conv.fctptr.md)) conversion, can be performed explicitly using `static_cast`. A program is ill-formed if it uses `static_cast` to perform the inverse of an ill-formed standard conversion sequence. [*Example 2*:

```cpp
struct B { };
struct D : private B { };
void f() {
  static_cast<D*>((B*)0);               // error: B is a private base of D
  static_cast<int B::*>((int D::*)0);   // error: B is a private base of D
}
```

— *end example*]

**8** The lvalue-to-rvalue ([[conv.lval]](conv.lval.md)), array-to-pointer ([[conv.array]](conv.array.md)), and function-to-pointer ([[conv.func]](conv.func.md)) conversions are applied to the operand. Such a `static_cast` is subject to the restriction that the explicit conversion does not cast away constness ([[expr.const.cast]](expr.const.cast.md)), and the following additional rules for specific cases:

**9** A value of a scoped enumeration type ([[dcl.enum]](../dcl/dcl.enum.md)) can be explicitly converted to an integral type; the result is the same as that of converting to the enumeration’s underlying type and then to the destination type. A value of a scoped enumeration type can also be explicitly converted to a floating-point type; the result is the same as that of converting from the original value to the floating-point type.

**10** A value of integral or enumeration type can be explicitly converted to a complete enumeration type. If the enumeration type has a fixed underlying type, the value is first converted to that type by integral conversion, if necessary, and then to the enumeration type. If the enumeration type does not have a fixed underlying type, the value is unchanged if the original value is within the range of the enumeration values ([[dcl.enum]](../dcl/dcl.enum.md)), and otherwise, the behavior is undefined. A value of floating-point type can also be explicitly converted to an enumeration type. The resulting value is the same as converting the original value to the underlying type of the enumeration ([[conv.fpint]](conv.fpint.md)), and subsequently to the enumeration type.

**11** A prvalue of type “pointer to *cv1* `B`”, where `B` is a class type, can be converted to a prvalue of type “pointer to *cv2* `D`”, where `D` is a complete class derived ([[class.derived]](../class/class.derived.md)) from `B`, if *cv2* is the same cv-qualification as, or greater cv-qualification than, *cv1*. If `B` is a virtual base class of `D` or a base class of a virtual base class of `D`, or if no valid standard conversion from “pointer to `D`” to “pointer to `B`” exists ([[conv.ptr]](conv.ptr.md)), the program is ill-formed. The null pointer value ([[basic.compound]](../basic/basic.compound.md)) is converted to the null pointer value of the destination type. If the prvalue of type “pointer to *cv1* `B`” points to a `B` that is actually a subobject of an object of type `D`, the resulting pointer points to the enclosing object of type `D`. Otherwise, the behavior is undefined.

**12** A prvalue of type “pointer to member of `D` of type *cv1* `T`” can be converted to a prvalue of type “pointer to member of `B` of type *cv2* `T`”, where `D` is a complete class type and `B` is a base class ([[class.derived]](../class/class.derived.md)) of `D`, if *cv2* is the same cv-qualification as, or greater cv-qualification than, *cv1*. [*Note 4*: Function types (including those used in pointer-to-member-function types) are never cv-qualified ([[dcl.fct]](../dcl/dcl.fct.md)). — *end note*] If no valid standard conversion from “pointer to member of `B` of type `T`” to “pointer to member of `D` of type `T`” exists ([[conv.mem]](conv.mem.md)), the program is ill-formed. The null member pointer value ([[conv.mem]](conv.mem.md)) is converted to the null member pointer value of the destination type. If class `B` contains the original member, or is a base or derived class of the class containing the original member, the resulting pointer to member points to the original member. Otherwise, the behavior is undefined. [*Note 5*: Although class `B` need not contain the original member, the dynamic type of the object with which indirection through the pointer to member is performed must contain the original member; see [[expr.mptr.oper]](expr.mptr.oper.md). — *end note*]

**13** A prvalue of type “pointer to *cv1* `void`” can be converted to a prvalue of type “pointer to *cv2* `T`”, where `T` is an object type and *cv2* is the same cv-qualification as, or greater cv-qualification than, *cv1*. If the original pointer value represents the address `A` of a byte in memory and `A` does not satisfy the alignment requirement of `T`, then the resulting pointer value is unspecified. Otherwise, if the original pointer value points to an object *a*, and there is an object *b* of type `T` (ignoring cv-qualification) that is pointer-interconvertible ([[basic.compound]](../basic/basic.compound.md)) with *a*, the result is a pointer to *b*. Otherwise, the pointer value is unchanged by the conversion. [*Example 3*:

```cpp
T* p1 = new T;
const T* p2 = static_cast<const T*>(static_cast<void*>(p1));
bool b = p1 == p2;  // b will have the value true.
```

— *end example*]
