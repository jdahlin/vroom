# 6.3 One-definition rule [basic.def.odr]

[Contents](../README.md) · up: [6 Basics \[basic\]](basic.md) · prev: [6.2 Declarations and definitions \[basic.def\]](basic.def.md) · next: [6.4 Scope \[basic.scope\]](basic.scope.md)

**1** No translation unit shall contain more than one definition of any variable, function, class type, enumeration type, template, default argument for a parameter (for a function in a given scope), or default template argument.

**2** An expression or conversion is *potentially evaluated* unless it is an unevaluated operand ([[expr.prop]](../expr/expr.prop.md)), a subexpression thereof, or a conversion in an initialization or conversion sequence in such a context. The set of *potential results* of an expression *E* is defined as follows:

- If *E* is an *id-expression* ([[expr.prim.id]](../expr/expr.prim.id.md)), the set contains only *E*.
- If *E* is a subscripting operation ([[expr.sub]](../expr/expr.sub.md)) with an array operand, the set contains the potential results of that operand.
- If *E* is a class member access expression ([[expr.ref]](../expr/expr.ref.md)) of the form *E*<sub>1</sub> `.` `template`ₒₚₜ *E*<sub>2</sub> naming a non-static data member, the set contains the potential results of *E*<sub>1</sub>.
- If *E* is a class member access expression naming a static data member, the set contains the *id-expression* designating the data member.
- If *E* is a pointer-to-member expression ([[expr.mptr.oper]](../expr/expr.mptr.oper.md)) of the form *E*<sub>1</sub> `.*` *E*<sub>2</sub>, the set contains the potential results of *E*<sub>1</sub>.
- If *E* has the form `(E_1)`, the set contains the potential results of *E*<sub>1</sub>.
- If *E* is a glvalue conditional expression ([[expr.cond]](../expr/expr.cond.md)), the set is the union of the sets of potential results of the second and third operands.
- If *E* is a comma expression ([[expr.comma]](../expr/expr.comma.md)), the set contains the potential results of the right operand.
- Otherwise, the set is empty.

[*Note 1*: This set is a (possibly-empty) set of *id-expression*s, each of which is either *E* or a subexpression of *E*. [*Example 1*: In the following example, the set of potential results of the initializer of `n` contains the first `S::x` subexpression, but not the second `S::x` subexpression.

```cpp
struct S { static const int x = 0; };
const int &f(const int &r);
int n = b ? (1, S::x)           // S::x is not odr-used here
          : f(S::x);            // S::x is odr-used here, so a definition is required
```

— *end example*] — *end note*]

**3** A function is *named by* an expression or conversion as follows:

- A function is named by an expression or conversion if it is the selected member of an overload set ([[basic.lookup]](basic.lookup.md), [[over.match]](../over/over.match.md), [[over.over]](../over/over.over.md)) in an overload resolution performed as part of forming that expression or conversion, unless it is a pure virtual function and either the expression is not an *id-expression* naming the function with an explicitly qualified name or the expression forms a pointer to member ([[expr.unary.op]](../expr/expr.unary.op.md)). [*Note 2*: This covers taking the address of functions ([[conv.func]](../expr/conv.func.md), [[expr.unary.op]](../expr/expr.unary.op.md)), calls to named functions ([[expr.call]](../expr/expr.call.md)), operator overloading ([[over]](../over/over.md)), user-defined conversions ([[class.conv.fct]](../class/class.conv.fct.md)), allocation functions for *new-expression*s ([[expr.new]](../expr/expr.new.md)), as well as non-default initialization ([[dcl.init]](../dcl/dcl.init.md)). A constructor selected to copy or move an object of class type is considered to be named by an expression or conversion even if the call is actually elided by the implementation ([[class.copy.elision]](../class/class.copy.elision.md)). — *end note*]
- A deallocation function for a class is named by a *new-expression* if it is the single matching deallocation function for the allocation function selected by overload resolution, as specified in [[expr.new]](../expr/expr.new.md).
- A deallocation function for a class is named by a *delete-expression* if it is the selected usual deallocation function as specified in [[expr.delete]](../expr/expr.delete.md) and [[class.free]](../class/class.free.md).

**4** A variable `x` whose name appears as a potentially-evaluated expression *E* is *odr-used* by *E* unless

- `x` is a reference that is usable in constant expressions ([[expr.const]](../expr/expr.const.md)), or
- `x` is a variable of non-reference type that is usable in constant expressions and has no mutable subobjects, and *E* is an element of the set of potential results of an expression of non-volatile-qualified non-class type to which the lvalue-to-rvalue conversion ([[conv.lval]](../expr/conv.lval.md)) is applied, or
- `x` is a variable of non-reference type, and *E* is an element of the set of potential results of a discarded-value expression ([[expr.prop]](../expr/expr.prop.md)) to which the lvalue-to-rvalue conversion is not applied.

**5** A structured binding is odr-used if it appears as a potentially-evaluated expression.

**6** `*this` is odr-used if `this` appears as a potentially-evaluated expression (including as the result of the implicit transformation in the body of a non-static member function ([[class.mfct.non-static]](../class/class.mfct.non-static.md))).

**7** A virtual member function is odr-used if it is not pure. A function is odr-used if it is named by a potentially-evaluated expression or conversion. A non-placement allocation or deallocation function for a class is odr-used by the definition of a constructor of that class. A non-placement deallocation function for a class is odr-used by the definition of the destructor of that class, or by being selected by the lookup at the point of definition of a virtual destructor ([[class.dtor]](../class/class.dtor.md)).[^1]

**8** An assignment operator function in a class is odr-used by an implicitly-defined copy-assignment or move-assignment function for another class as specified in [[class.copy.assign]](../class/class.copy.assign.md). A constructor for a class is odr-used as specified in [[dcl.init]](../dcl/dcl.init.md). A destructor for a class is odr-used if it is potentially invoked ([[class.dtor]](../class/class.dtor.md)).

**9** A local entity ([[basic.pre]](basic.pre.md)) is *odr-usable* in a declarative region ([[basic.scope.declarative]](basic.scope.declarative.md)) if:

- either the local entity is not `*this`, or an enclosing class or non-lambda function parameter scope exists and, if the innermost such scope is a function parameter scope, it corresponds to a non-static member function, and

- for each intervening declarative region ([[basic.scope.declarative]](basic.scope.declarative.md)) between the point at which the entity is introduced and the region (where `*this` is considered to be introduced within the innermost enclosing class or non-lambda function definition scope), either:

  - the intervening declarative region is a block scope, or
  - the intervening declarative region is the function parameter scope of a *lambda-expression* that has a *simple-capture* naming the entity or has a *capture-default*, and the block scope of the *lambda-expression* is also an intervening declarative region.

If a local entity is odr-used in a declarative region in which it is not odr-usable, the program is ill-formed. [*Example 2*:

```cpp
void f(int n) {
  [] { n = 1; };                // error: n is not odr-usable due to intervening lambda-expression
  struct A {
    void f() { n = 2; }         // error: n is not odr-usable due to intervening function definition scope
  };
  void g(int = n);              // error: n is not odr-usable due to intervening function parameter scope
  [=](int k = n) {};            // error: n is not odr-usable due to being
                                // outside the block scope of the lambda-expression
  [&] { [n]{ return n; }; };    // OK
}
```

— *end example*]

**10** Every program shall contain exactly one definition of every non-inline function or variable that is odr-used in that program outside of a discarded statement ([[stmt.if]](../stmt/stmt.if.md)); no diagnostic required. The definition can appear explicitly in the program, it can be found in the standard or a user-defined library, or (when appropriate) it is implicitly defined (see [[class.default.ctor]](../class/class.default.ctor.md), [[class.copy.ctor]](../class/class.copy.ctor.md), [[class.dtor]](../class/class.dtor.md), and [[class.copy.assign]](../class/class.copy.assign.md)). [*Example 3*:

```cpp
auto f() {
  struct A {};
  return A{};
}
decltype(f()) g();
auto x = g();
```

A program containing this translation unit is ill-formed because `g` is odr-used but not defined, and cannot be defined in any other translation unit because the local class `A` cannot be named outside this translation unit. — *end example*]

**11** A *definition domain* is a *private-module-fragment* or the portion of a translation unit excluding its *private-module-fragment* (if any). A definition of an inline function or variable shall be reachable from the end of every definition domain in which it is odr-used outside of a discarded statement.

**12** A definition of a class is required to be reachable in every context in which the class is used in a way that requires the class type to be complete. [*Example 4*: The following complete translation unit is well-formed, even though it never defines `X`:

```cpp
struct X;                       // declare X as a struct type
struct X* x1;                   // use X in pointer formation
X* x2;                          // use X in pointer formation
```

— *end example*] [*Note 3*: The rules for declarations and expressions describe in which contexts complete class types are required. A class type `T` must be complete if:

- an object of type `T` is defined ([[basic.def]](basic.def.md)), or
- a non-static class data member of type `T` is declared ([[class.mem]](../class/class.mem.md)), or
- `T` is used as the allocated type or array element type in a *new-expression* ([[expr.new]](../expr/expr.new.md)), or
- an lvalue-to-rvalue conversion is applied to a glvalue referring to an object of type `T` ([[conv.lval]](../expr/conv.lval.md)), or
- an expression is converted (either implicitly or explicitly) to type `T` ([[conv]](../expr/conv.md), [[expr.type.conv]](../expr/expr.type.conv.md), [[expr.dynamic.cast]](../expr/expr.dynamic.cast.md), [[expr.static.cast]](../expr/expr.static.cast.md), [[expr.cast]](../expr/expr.cast.md)), or
- an expression that is not a null pointer constant, and has type other than *cv* `void*`, is converted to the type pointer to `T` or reference to `T` using a standard conversion ([[conv]](../expr/conv.md)), a `dynamic_cast` ([[expr.dynamic.cast]](../expr/expr.dynamic.cast.md)) or a `static_cast` ([[expr.static.cast]](../expr/expr.static.cast.md)), or
- a class member access operator is applied to an expression of type `T` ([[expr.ref]](../expr/expr.ref.md)), or
- the `typeid` operator ([[expr.typeid]](../expr/expr.typeid.md)) or the `sizeof` operator ([[expr.sizeof]](../expr/expr.sizeof.md)) is applied to an operand of type `T`, or
- a function with a return type or argument type of type `T` is defined ([[basic.def]](basic.def.md)) or called ([[expr.call]](../expr/expr.call.md)), or
- a class with a base class of type `T` is defined ([[class.derived]](../class/class.derived.md)), or
- an lvalue of type `T` is assigned to ([[expr.ass]](../expr/expr.ass.md)), or
- the type `T` is the subject of an `alignof` expression ([[expr.alignof]](../expr/expr.alignof.md)), or
- an *exception-declaration* has type `T`, reference to `T`, or pointer to `T` ([[except.handle]](../except/except.handle.md)).

— *end note*]

**13** There can be more than one definition of a

- class type ([[class]](../class/class.md)),
- enumeration type ([[dcl.enum]](../dcl/dcl.enum.md)),
- inline function or variable ([[dcl.inline]](../dcl/dcl.inline.md)),
- templated entity ([[temp.pre]](../temp/temp.pre.md)),
- default argument for a parameter (for a function in a given scope) ([[dcl.fct.default]](../dcl/dcl.fct.default.md)), or
- default template argument ([[temp.param]](../temp/temp.param.md))

in a program provided that each definition appears in a different translation unit and the definitions satisfy the following requirements. Given such an entity `D` defined in more than one translation unit, for all definitions of `D`, or, if `D` is an unnamed enumeration, for all definitions of `D` that are reachable at any given program point, the following requirements shall be satisfied.

- Each such definition shall not be attached to a named module ([[module.unit]](../module/module.unit.md)).

- Each such definition shall consist of the same sequence of tokens, where the definition of a closure type is considered to consist of the sequence of tokens of the corresponding *lambda-expression*.

- In each such definition, corresponding names, looked up according to [[basic.lookup]](basic.lookup.md), shall refer to the same entity, after overload resolution ([[over.match]](../over/over.match.md)) and after matching of partial template specialization ([[temp.over]](../temp/temp.over.md)), except that a name can refer to

  - a non-volatile const object with internal or no linkage if the object

    - has the same literal type in all definitions of `D`,
    - is initialized with a constant expression ([[expr.const]](../expr/expr.const.md)),
    - is not odr-used in any definition of `D`, and
    - has the same value in all definitions of `D`,

    or

  - a reference with internal or no linkage initialized with a constant expression such that the reference refers to the same entity in all definitions of `D`.

- In each such definition, except within the default arguments and default template arguments of `D`, corresponding *lambda-expression*s shall have the same closure type (see below).

- In each such definition, corresponding entities shall have the same language linkage.

- In each such definition, the overloaded operators referred to, the implicit calls to conversion functions, constructors, operator new functions and operator delete functions, shall refer to the same function.

- In each such definition, a default argument used by an (implicit or explicit) function call or a default template argument used by an (implicit or explicit) *template-id* or *simple-template-id* is treated as if its token sequence were present in the definition of `D`; that is, the default argument or default template argument is subject to the requirements described in this paragraph (recursively).

- If `D` is a class with an implicitly-declared constructor ([[class.default.ctor]](../class/class.default.ctor.md), [[class.copy.ctor]](../class/class.copy.ctor.md)), it is as if the constructor was implicitly defined in every translation unit where it is odr-used, and the implicit definition in every translation unit shall call the same constructor for a subobject of `D`. [*Example 5*:

  ```cpp
  // translation unit 1:
  struct X {
    X(int, int);
    X(int, int, int);
  };
  X::X(int, int = 0) { }
  class D {
    X x = 0;
  };
  D d1;                           // X(int, int) called by D()

  // translation unit 2:
  struct X {
    X(int, int);
    X(int, int, int);
  };
  X::X(int, int = 0, int = 0) { }
  class D {
    X x = 0;
  };
  D d2;                           // X(int, int, int) called by D();
                                  // D()'s implicit definition violates the ODR
  ```

  — *end example*]

- If `D` is a class with a defaulted three-way comparison operator function ([[class.spaceship]](../class/class.spaceship.md)), it is as if the operator was implicitly defined in every translation unit where it is odr-used, and the implicit definition in every translation unit shall call the same comparison operators for each subobject of `D`.

**14** If `D` is a template and is defined in more than one translation unit, then the preceding requirements shall apply both to names from the template’s enclosing scope used in the template definition ([[temp.nondep]](../temp/temp.nondep.md)), and also to dependent names at the point of instantiation ([[temp.dep]](../temp/temp.dep.md)). These requirements also apply to corresponding entities defined within each definition of `D` (including the closure types of *lambda-expression*s, but excluding entities defined within default arguments or default template arguments of either `D` or an entity not defined within `D`). For each such entity and for `D` itself, the behavior is as if there is a single entity with a single definition, including in the application of these requirements to other entities. [*Note 4*: The entity is still declared in multiple translation units, and [[basic.link]](basic.link.md) still applies to these declarations. In particular, *lambda-expression*s ([[expr.prim.lambda]](../expr/expr.prim.lambda.md)) appearing in the type of `D` may result in the different declarations having distinct types, and *lambda-expression*s appearing in a default argument of `D` may still denote different types in different translation units. — *end note*]

**15** If these definitions do not satisfy these requirements, then the program is ill-formed; a diagnostic is required only if the entity is attached to a named module and a prior definition is reachable at the point where a later definition occurs.

**16** [*Example 6*:

```cpp
inline void f(bool cond, void (*p)()) {
  if (cond) f(false, []{});
}
inline void g(bool cond, void (*p)() = []{}) {
  if (cond) g(false);
}
struct X {
  void h(bool cond, void (*p)() = []{}) {
    if (cond) h(false);
  }
};
```

If the definition of `f` appears in multiple translation units, the behavior of the program is as if there is only one definition of `f`. If the definition of `g` appears in multiple translation units, the program is ill-formed (no diagnostic required) because each such definition uses a default argument that refers to a distinct *lambda-expression* closure type. The definition of `X` can appear in multiple translation units of a valid program; the *lambda-expression*s defined within the default argument of `X::h` within the definition of `X` denote the same closure type in each translation unit. — *end example*]

**17** If, at any point in the program, there is more than one reachable unnamed enumeration definition in the same scope that have the same first enumerator name and do not have typedef names for linkage purposes ([[dcl.enum]](../dcl/dcl.enum.md)), those unnamed enumeration types shall be the same; no diagnostic required.

[^1]: An implementation is not required to call allocation and deallocation functions from constructors or destructors; however, this is a permissible implementation technique.
