# 7.6.2.1 Unary operators [expr.unary.op]

[Contents](../README.md) · up: [7.6.2 Unary expressions \[expr.unary\]](expr.unary.md) · prev: [7.6.2 Unary expressions \[expr.unary\]](expr.unary.md) · next: [7.6.2.2 Increment and decrement \[expr.pre.incr\]](expr.pre.incr.md)

**1** The unary `*` operator performs *indirection*: the expression to which it is applied shall be a pointer to an object type, or a pointer to a function type and the result is an lvalue referring to the object or function to which the expression points. If the type of the expression is “pointer to `T`”, the type of the result is “`T`”. [*Note 1*: Indirection through a pointer to an incomplete type (other than *cv* `void`) is valid. The lvalue thus obtained can be used in limited ways (to initialize a reference, for example); this lvalue must not be converted to a prvalue, see [[conv.lval]](conv.lval.md). — *end note*]

**2** The result of each of the following unary operators is a prvalue.

**3** The result of the unary `&` operator is a pointer to its operand.

- If the operand is a *qualified-id* naming a non-static or variant member `m` of some class `C` with type `T`, the result has type “pointer to member of class `C` of type `T`” and is a prvalue designating `C::m`.
- Otherwise, if the operand is an lvalue of type `T`, the resulting expression is a prvalue of type “pointer to `T`” whose result is a pointer to the designated object ([[intro.memory]](../basic/intro.memory.md)) or function. [*Note 2*: In particular, taking the address of a variable of type “*cv* `T`” yields a pointer of type “pointer to *cv* `T`”. — *end note*]
- Otherwise, the program is ill-formed.

[*Example 1*:

```cpp
struct A { int i; };
struct B : A { };
... &B::i ...       // has type int A::*
int a;
int* p1 = &a;
int* p2 = p1 + 1;   // defined behavior
bool b = p2 > p1;   // defined behavior, with value true
```

— *end example*] [*Note 3*: A pointer to member formed from a `mutable` non-static data member ([[dcl.stc]](../dcl/dcl.stc.md)) does not reflect the `mutable` specifier associated with the non-static data member. — *end note*]

**4** A pointer to member is only formed when an explicit `&` is used and its operand is a *qualified-id* not enclosed in parentheses. [*Note 4*: That is, the expression `&(qualified-id)`, where the *qualified-id* is enclosed in parentheses, does not form an expression of type “pointer to member”. Neither does `qualified-id`, because there is no implicit conversion from a *qualified-id* for a non-static member function to the type “pointer to member function” as there is from an lvalue of function type to the type “pointer to function” ([[conv.func]](conv.func.md)). Nor is `&unqualified-id` a pointer to member, even within the scope of the *unqualified-id*’s class. — *end note*]

**5** If `&` is applied to an lvalue of incomplete class type and the complete type declares `operator&()`, it is unspecified whether the operator has the built-in meaning or the operator function is called. The operand of `&` shall not be a bit-field.

**6** [*Note 5*: The address of an overloaded function ([[over]](../over/over.md)) can be taken only in a context that uniquely determines which version of the overloaded function is referred to (see [[over.over]](../over/over.over.md)). Since the context might determine whether the operand is a static or non-static member function, the context can also affect whether the expression has type “pointer to function” or “pointer to member function”. — *end note*]

**7** The operand of the unary `+` operator shall have arithmetic, unscoped enumeration, or pointer type and the result is the value of the argument. Integral promotion is performed on integral or enumeration operands. The type of the result is the type of the promoted operand.

**8** The operand of the unary `-` operator shall have arithmetic or unscoped enumeration type and the result is the negation of its operand. Integral promotion is performed on integral or enumeration operands. The negative of an unsigned quantity is computed by subtracting its value from 2<sup>*n*</sup>, where *n* is the number of bits in the promoted operand. The type of the result is the type of the promoted operand.

**9** The operand of the logical negation operator `!` is contextually converted to `bool` ([[conv]](conv.md)); its value is `true` if the converted operand is `false` and `false` otherwise. The type of the result is `bool`.

**10** The operand of `~` shall have integral or unscoped enumeration type; the result is the ones’ complement of its operand. Integral promotions are performed. The type of the result is the type of the promoted operand. There is an ambiguity in the grammar when `~` is followed by a *type-name* or *decltype-specifier*. The ambiguity is resolved by treating `~` as the unary complement operator rather than as the start of an *unqualified-id* naming a destructor. [*Note 6*: Because the grammar does not permit an operator to follow the `.`, `->`, or `::` tokens, a `~` followed by a *type-name* or *decltype-specifier* in a member access expression or *qualified-id* is unambiguously parsed as a destructor name. — *end note*]
