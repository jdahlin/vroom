# 7.6.1.4 Class member access [expr.ref]

[Contents](../README.md) · up: [7.6.1 Postfix expressions \[expr.post\]](expr.post.md) · prev: [7.6.1.3 Explicit type conversion (functional notation) \[expr.type.conv\]](expr.type.conv.md) · next: [7.6.1.5 Increment and decrement \[expr.post.incr\]](expr.post.incr.md)

**1** A postfix expression followed by a dot `.` or an arrow `->`, optionally followed by the keyword `template` ([[temp.names]](../temp/temp.names.md)), and then followed by an *id-expression*, is a postfix expression. The postfix expression before the dot or arrow is evaluated;[^1] the result of that evaluation, together with the *id-expression*, determines the result of the entire postfix expression.

**2** For the first option (dot) the first expression shall be a glvalue. For the second option (arrow) the first expression shall be a prvalue having pointer type. The expression `E1->E2` is converted to the equivalent form `(*(E1)).E2`; the remainder of [[expr.ref]](expr.ref.md) will address only the first option (dot).[^2]

**3** Abbreviating *postfix-expression*`.`*id-expression* as `E1.E2`, `E1` is called the *object expression*. If the object expression is of scalar type, `E2` shall name the pseudo-destructor of that same type (ignoring cv-qualifications) and `E1.E2` is an lvalue of type “function of () returning `void`”. [*Note 1*: This value can only be used for a notional function call ([[expr.prim.id.dtor]](expr.prim.id.dtor.md)). — *end note*]

**4** Otherwise, the object expression shall be of class type. The class type shall be complete unless the class member access appears in the definition of that class. [*Note 2*: If the class is incomplete, lookup in the complete class type is required to refer to the same declaration ([[basic.scope.class]](../basic/basic.scope.class.md)). — *end note*] The *id-expression* shall name a member of the class or of one of its base classes. [*Note 3*: Because the name of a class is inserted in its class scope ([[class]](../class/class.md)), the name of a class is also considered a nested member of that class. — *end note*] [*Note 4*: [[basic.lookup.classref]](../basic/basic.lookup.classref.md) describes how names are looked up after the `.` and `->` operators. — *end note*]

**5** If `E2` is a bit-field, `E1.E2` is a bit-field. The type and value category of `E1.E2` are determined as follows. In the remainder of [[expr.ref]](expr.ref.md), *cq* represents either `const` or the absence of `const` and *vq* represents either `volatile` or the absence of `volatile`. *cv* represents an arbitrary set of cv-qualifiers, as defined in [[basic.type.qualifier]](../basic/basic.type.qualifier.md).

**6** If `E2` is declared to have type “reference to `T`”, then `E1.E2` is an lvalue; the type of `E1.E2` is `T`. Otherwise, one of the following rules applies.

- If `E2` is a static data member and the type of `E2` is `T`, then `E1.E2` is an lvalue; the expression designates the named member of the class. The type of `E1.E2` is `T`.

- If `E2` is a non-static data member and the type of `E1` is “*cq1 vq1* `X`”, and the type of `E2` is “*cq2 vq2* `T`”, the expression designates the corresponding member subobject of the object designated by the first expression. If `E1` is an lvalue, then `E1.E2` is an lvalue; otherwise `E1.E2` is an xvalue. Let the notation *vq12* stand for the “union” of *vq1* and *vq2*; that is, if *vq1* or *vq2* is `volatile`, then *vq12* is `volatile`. Similarly, let the notation *cq12* stand for the “union” of *cq1* and *cq2*; that is, if *cq1* or *cq2* is `const`, then *cq12* is `const`. If `E2` is declared to be a `mutable` member, then the type of `E1.E2` is “*vq12* `T`”. If `E2` is not declared to be a `mutable` member, then the type of `E1.E2` is “*cq12* *vq12* `T`”.

- If `E2` is a (possibly overloaded) member function, function overload resolution ([[over.match]](../over/over.match.md)) is used to select the function to which `E2` refers. The type of `E1.E2` is the type of `E2` and `E1.E2` refers to the function referred to by `E2`.

  - If `E2` refers to a static member function, `E1.E2` is an lvalue.
  - Otherwise (when `E2` refers to a non-static member function), `E1.E2` is a prvalue. The expression can be used only as the left-hand operand of a member function call ([[class.mfct]](../class/class.mfct.md)). [*Note 5*: Any redundant set of parentheses surrounding the expression is ignored ([[expr.prim.paren]](expr.prim.paren.md)). — *end note*]

- If `E2` is a nested type, the expression `E1.E2` is ill-formed.

- If `E2` is a member enumerator and the type of `E2` is `T`, the expression `E1.E2` is a prvalue. The type of `E1.E2` is `T`.

**7** If `E2` is a non-static data member or a non-static member function, the program is ill-formed if the class of which `E2` is directly a member is an ambiguous base ([[class.member.lookup]](../class/class.member.lookup.md)) of the naming class ([[class.access.base]](../class/class.access.base.md)) of `E2`. [*Note 6*: The program is also ill-formed if the naming class is an ambiguous base of the class type of the object expression; see [[class.access.base]](../class/class.access.base.md). — *end note*]

[^1]: If the class member access expression is evaluated, the subexpression evaluation happens even if the result is unnecessary to determine the value of the entire postfix expression, for example if the *id-expression* denotes a static member.
[^2]: Note that `(*(E1))` is an lvalue.
