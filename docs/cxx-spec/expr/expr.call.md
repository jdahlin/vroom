# 7.6.1.2 Function call [expr.call]

[Contents](../README.md) · up: [7.6.1 Postfix expressions \[expr.post\]](expr.post.md) · prev: [7.6.1.1 Subscripting \[expr.sub\]](expr.sub.md) · next: [7.6.1.3 Explicit type conversion (functional notation) \[expr.type.conv\]](expr.type.conv.md)

**1** A function call is a postfix expression followed by parentheses containing a possibly empty, comma-separated list of *initializer-clause*s which constitute the arguments to the function. [*Note 1*: If the postfix expression is a function or member function name, the appropriate function and the validity of the call are determined according to the rules in [[over.match]](../over/over.match.md). — *end note*] The postfix expression shall have function type or function pointer type. For a call to a non-member function or to a static member function, the postfix expression shall either be an lvalue that refers to a function (in which case the function-to-pointer standard conversion ([[conv.func]](conv.func.md)) is suppressed on the postfix expression), or have function pointer type.

**2** For a call to a non-static member function, the postfix expression shall be an implicit ([[class.mfct.non-static]](../class/class.mfct.non-static.md), [[class.static]](../class/class.static.md)) or explicit class member access ([[expr.ref]](expr.ref.md)) whose *id-expression* is a function member name, or a pointer-to-member expression ([[expr.mptr.oper]](expr.mptr.oper.md)) selecting a function member; the call is as a member of the class object referred to by the object expression. In the case of an implicit class member access, the implied object is the one pointed to by `this`. [*Note 2*: A member function call of the form `f()` is interpreted as `(*this).f()` (see [[class.mfct.non-static]](../class/class.mfct.non-static.md)). — *end note*]

**3** If the selected function is non-virtual, or if the *id-expression* in the class member access expression is a *qualified-id*, that function is called. Otherwise, its final overrider ([[class.virtual]](../class/class.virtual.md)) in the dynamic type of the object expression is called; such a call is referred to as a *virtual function call*. [*Note 3*: The dynamic type is the type of the object referred to by the current value of the object expression. [[class.cdtor]](../class/class.cdtor.md) describes the behavior of virtual function calls when the object expression refers to an object under construction or destruction. — *end note*]

**4** [*Note 4*: If a function or member function name is used, and name lookup ([[basic.lookup]](../basic/basic.lookup.md)) does not find a declaration of that name, the program is ill-formed. No function is implicitly declared by such a call. — *end note*]

**5** If the *postfix-expression* names a destructor or pseudo-destructor ([[expr.prim.id.dtor]](expr.prim.id.dtor.md)), the type of the function call expression is `void`; otherwise, the type of the function call expression is the return type of the statically chosen function (i.e., ignoring the `virtual` keyword), even if the type of the function actually called is different. This return type shall be an object type, a reference type or *cv* `void`. If the *postfix-expression* names a pseudo-destructor (in which case the *postfix-expression* is a possibly-parenthesized class member access), the function call destroys the object of scalar type denoted by the object expression of the class member access ([[expr.ref]](expr.ref.md), [[basic.life]](../basic/basic.life.md)).

**6** Calling a function through an expression whose function type is different from the function type of the called function’s definition results in undefined behavior.

**7** When a function is called, each parameter ([[dcl.fct]](../dcl/dcl.fct.md)) is initialized ([[dcl.init]](../dcl/dcl.init.md), [[class.copy.ctor]](../class/class.copy.ctor.md)) with its corresponding argument. If there is no corresponding argument, the default argument for the parameter is used. [*Example 1*:

```cpp
template<typename ...T> int f(int n = 0, T ...t);
int x = f<int>();               // error: no argument for second function parameter
```

— *end example*] If the function is a non-static member function, the `this` parameter of the function ([[class.this]](../class/class.this.md)) is initialized with a pointer to the object of the call, converted as if by an explicit type conversion ([[expr.cast]](expr.cast.md)). [*Note 5*: There is no access or ambiguity checking on this conversion; the access checking and disambiguation are done as part of the (possibly implicit) class member access operator. See [[class.member.lookup]](../class/class.member.lookup.md), [[class.access.base]](../class/class.access.base.md), and [[expr.ref]](expr.ref.md). — *end note*] When a function is called, the type of any parameter shall not be a class type that is either incomplete or abstract. [*Note 6*: This still allows a parameter to be a pointer or reference to such a type. However, it prevents a passed-by-value parameter to have an incomplete or abstract class type. — *end note*] It is implementation-defined whether the lifetime of a parameter ends when the function in which it is defined returns or at the end of the enclosing full-expression. The initialization and destruction of each parameter occurs within the context of the calling function. [*Example 2*: The access of the constructor, conversion functions or destructor is checked at the point of call in the calling function. If a constructor or destructor for a function parameter throws an exception, the search for a handler starts in the scope of the calling function; in particular, if the function called has a *function-try-block* ([[except.pre]](../except/except.pre.md)) with a handler that could handle the exception, this handler is not considered. — *end example*]

**8** The *postfix-expression* is sequenced before each *expression* in the *expression-list* and any default argument. The initialization of a parameter, including every associated value computation and side effect, is indeterminately sequenced with respect to that of any other parameter. [*Note 7*: All side effects of argument evaluations are sequenced before the function is entered (see [[intro.execution]](../basic/intro.execution.md)). — *end note*] [*Example 3*:

```cpp
void f() {
  std::string s = "but I have heard it works even if you don't believe in it";
  s.replace(0, 4, "").replace(s.find("even"), 4, "only").replace(s.find(" don't"), 6, "");
  assert(s == "I have heard it works only if you believe in it");       // OK
}
```

— *end example*] [*Note 8*: If an operator function is invoked using operator notation, argument evaluation is sequenced as specified for the built-in operator; see [[over.match.oper]](../over/over.match.oper.md). — *end note*] [*Example 4*:

```cpp
struct S {
  S(int);
};
int operator<<(S, int);
int i, j;
int x = S(i=1) << (i=2);
int y = operator<<(S(j=1), j=2);
```

After performing the initializations, the value of `i` is 2 (see [[expr.shift]](expr.shift.md)), but it is unspecified whether the value of `j` is 1 or 2. — *end example*]

**9** The result of a function call is the result of the possibly-converted operand of the `return` statement ([[stmt.return]](../stmt/stmt.return.md)) that transferred control out of the called function (if any), except in a virtual function call if the return type of the final overrider is different from the return type of the statically chosen function, the value returned from the final overrider is converted to the return type of the statically chosen function.

**10** [*Note 9*: A function can change the values of its non-const parameters, but these changes cannot affect the values of the arguments except where a parameter is of a reference type ([[dcl.ref]](../dcl/dcl.ref.md)); if the reference is to a const-qualified type, `const_cast` is required to be used to cast away the constness in order to modify the argument’s value. Where a parameter is of `const` reference type a temporary object is introduced if needed ([[dcl.type]](../dcl/dcl.type.md), [[lex.literal]](../lex/lex.literal.md), [[lex.string]](../lex/lex.string.md), [[dcl.array]](../dcl/dcl.array.md), [[class.temporary]](../basic/class.temporary.md)). In addition, it is possible to modify the values of non-constant objects through pointer parameters. — *end note*]

**11** A function can be declared to accept fewer arguments (by declaring default arguments ([[dcl.fct.default]](../dcl/dcl.fct.default.md))) or more arguments (by using the ellipsis, `...`, or a function parameter pack ([[dcl.fct]](../dcl/dcl.fct.md))) than the number of parameters in the function definition ([[dcl.fct.def]](../dcl/dcl.fct.def.md)). [*Note 10*: This implies that, except where the ellipsis (`...`) or a function parameter pack is used, a parameter is available for each argument. — *end note*]

**12** When there is no parameter for a given argument, the argument is passed in such a way that the receiving function can obtain the value of the argument by invoking `va_arg` ([[support.runtime]](../lib/support/support.runtime.md)). [*Note 11*: This paragraph does not apply to arguments passed to a function parameter pack. Function parameter packs are expanded during template instantiation ([[temp.variadic]](../temp/temp.variadic.md)), thus each such argument has a corresponding parameter when a function template specialization is actually called. — *end note*] The lvalue-to-rvalue ([[conv.lval]](conv.lval.md)), array-to-pointer ([[conv.array]](conv.array.md)), and function-to-pointer ([[conv.func]](conv.func.md)) standard conversions are performed on the argument expression. An argument that has type *cv* `std::nullptr_t` is converted to type `void*` ([[conv.ptr]](conv.ptr.md)). After these conversions, if the argument does not have arithmetic, enumeration, pointer, pointer-to-member, or class type, the program is ill-formed. Passing a potentially-evaluated argument of a scoped enumeration type or of a class type ([[class]](../class/class.md)) having an eligible non-trivial copy constructor, an eligible non-trivial move constructor, or a non-trivial destructor ([[special]](../class/special.md)), with no corresponding parameter, is conditionally-supported with implementation-defined semantics. If the argument has integral or enumeration type that is subject to the integral promotions ([[conv.prom]](conv.prom.md)), or a floating-point type that is subject to the floating-point promotion ([[conv.fpprom]](conv.fpprom.md)), the value of the argument is converted to the promoted type before the call. These promotions are referred to as the *default argument promotions*.

**13** Recursive calls are permitted, except to the `main` function ([[basic.start.main]](../basic/basic.start.main.md)).

**14** A function call is an lvalue if the result type is an lvalue reference type or an rvalue reference to function type, an xvalue if the result type is an rvalue reference to object type, and a prvalue otherwise.
