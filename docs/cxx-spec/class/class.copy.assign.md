# 11.4.5 Copy/move assignment operator [class.copy.assign]

[Contents](../README.md) · up: [11.4 Class members \[class.mem\]](class.mem.md) · prev: [11.4.4.2 Copy/move constructors \[class.copy.ctor\]](class.copy.ctor.md) · next: [11.4.6 Destructors \[class.dtor\]](class.dtor.md)

**1** A user-declared *copy* assignment operator `X::operator=` is a non-static non-template member function of class `X` with exactly one parameter of type `X`, `X&`, `const X&`, `volatile X&`, or `const volatile X&`.[^1] [*Note 1*: An overloaded assignment operator must be declared to have only one parameter; see [[over.ass]](../over/over.ass.md). — *end note*] [*Note 2*: More than one form of copy assignment operator may be declared for a class. — *end note*] [*Note 3*: If a class `X` only has a copy assignment operator with a parameter of type `X&`, an expression of type const `X` cannot be assigned to an object of type `X`. [*Example 1*:

```cpp
struct X {
  X();
  X& operator=(X&);
};
const X cx;
X x;
void f() {
  x = cx;           // error: X::operator=(X&) cannot assign cx into x
}
```

— *end example*] — *end note*]

**2** If the class definition does not explicitly declare a copy assignment operator, one is declared *implicitly*. If the class definition declares a move constructor or move assignment operator, the implicitly declared copy assignment operator is defined as deleted; otherwise, it is defined as defaulted ([[dcl.fct.def]](../dcl/dcl.fct.def.md)). The latter case is deprecated if the class has a user-declared copy constructor or a user-declared destructor ([[depr.impldec]](../depr/depr.impldec.md)). The implicitly-declared copy assignment operator for a class `X` will have the form

```cpp
X& X::operator=(const X&)
```

if

- each direct base class `B` of `X` has a copy assignment operator whose parameter is of type `const B&`, `const volatile B&`, or `B`, and
- for all the non-static data members of `X` that are of a class type `M` (or array thereof), each such class type has a copy assignment operator whose parameter is of type `const M&`, `const volatile M&`, or `M`.[^2]

Otherwise, the implicitly-declared copy assignment operator will have the form

```cpp
X& X::operator=(X&)
```

**3** A user-declared move assignment operator `X::operator=` is a non-static non-template member function of class `X` with exactly one parameter of type `X&&`, `const X&&`, `volatile X&&`, or `const volatile X&&`. [*Note 4*: An overloaded assignment operator must be declared to have only one parameter; see [[over.ass]](../over/over.ass.md). — *end note*] [*Note 5*: More than one form of move assignment operator may be declared for a class. — *end note*]

**4** If the definition of a class `X` does not explicitly declare a move assignment operator, one will be implicitly declared as defaulted if and only if

- `X` does not have a user-declared copy constructor,
- `X` does not have a user-declared move constructor,
- `X` does not have a user-declared copy assignment operator, and
- `X` does not have a user-declared destructor.

[*Example 2*: The class definition

```cpp
struct S {
  int a;
  S& operator=(const S&) = default;
};
```

will not have a default move assignment operator implicitly declared because the copy assignment operator has been user-declared. The move assignment operator may be explicitly defaulted.

```cpp
struct S {
  int a;
  S& operator=(const S&) = default;
  S& operator=(S&&) = default;
};
```

— *end example*]

**5** The implicitly-declared move assignment operator for a class `X` will have the form

```cpp
X& X::operator=(X&&)
```

**6** The implicitly-declared copy/move assignment operator for class `X` has the return type `X&`; it returns the object for which the assignment operator is invoked, that is, the object assigned to. An implicitly-declared copy/move assignment operator is an inline public member of its class.

**7** A defaulted copy/move assignment operator for class `X` is defined as deleted if `X` has:

- a variant member with a non-trivial corresponding assignment operator and `X` is a union-like class, or
- a non-static data member of `const` non-class type (or array thereof), or
- a non-static data member of reference type, or
- a direct non-static data member of class type `M` (or array thereof) or a direct base class `M` that cannot be copied/moved because overload resolution ([[over.match]](../over/over.match.md)), as applied to find `M`’s corresponding assignment operator, results in an ambiguity or a function that is deleted or inaccessible from the defaulted assignment operator.

[*Note 6*: A defaulted move assignment operator that is defined as deleted is ignored by overload resolution ([[over.match]](../over/over.match.md), [[over.over]](../over/over.over.md)). — *end note*]

**8** Because a copy/move assignment operator is implicitly declared for a class if not declared by the user, a base class copy/move assignment operator is always hidden by the corresponding assignment operator of a derived class ([[over.ass]](../over/over.ass.md)). A *using-declaration* ([[namespace.udecl]](../dcl/namespace.udecl.md)) that brings in from a base class an assignment operator with a parameter type that could be that of a copy/move assignment operator for the derived class is not considered an explicit declaration of such an operator and does not suppress the implicit declaration of the derived class operator; the operator introduced by the *using-declaration* is hidden by the implicitly-declared operator in the derived class.

**9** A copy/move assignment operator for class `X` is trivial if it is not user-provided and if:

- class `X` has no virtual functions ([[class.virtual]](class.virtual.md)) and no virtual base classes ([[class.mi]](class.mi.md)), and
- the assignment operator selected to copy/move each direct base class subobject is trivial, and
- for each non-static data member of `X` that is of class type (or array thereof), the assignment operator selected to copy/move that member is trivial;

otherwise the copy/move assignment operator is *non-trivial*.

**10** A copy/move assignment operator for a class `X` that is defaulted and not defined as deleted is *implicitly defined* when it is odr-used ([[basic.def.odr]](../basic/basic.def.odr.md)) (e.g., when it is selected by overload resolution to assign to an object of its class type), when it is needed for constant evaluation ([[expr.const]](../expr/expr.const.md)), or when it is explicitly defaulted after its first declaration. The implicitly-defined copy/move assignment operator is `constexpr` if

- `X` is a literal type, and
- the assignment operator selected to copy/move each direct base class subobject is a constexpr function, and
- for each non-static data member of `X` that is of class type (or array thereof), the assignment operator selected to copy/move that member is a constexpr function.

**11** Before the defaulted copy/move assignment operator for a class is implicitly defined, all non-user-provided copy/move assignment operators for its direct base classes and its non-static data members are implicitly defined. [*Note 7*: An implicitly-declared copy/move assignment operator has an implied exception specification ([[except.spec]](../except/except.spec.md)). — *end note*]

**12** The implicitly-defined copy/move assignment operator for a non-union class `X` performs memberwise copy/move assignment of its subobjects. The direct base classes of `X` are assigned first, in the order of their declaration in the *base-specifier-list*, and then the immediate non-static data members of `X` are assigned, in the order in which they were declared in the class definition. Let `x` be either the parameter of the function or, for the move operator, an xvalue referring to the parameter. Each subobject is assigned in the manner appropriate to its type:

- if the subobject is of class type, as if by a call to `operator=` with the subobject as the object expression and the corresponding subobject of `x` as a single function argument (as if by explicit qualification; that is, ignoring any possible virtual overriding functions in more derived classes);
- if the subobject is an array, each element is assigned, in the manner appropriate to the element type;
- if the subobject is of scalar type, the built-in assignment operator is used.

It is unspecified whether subobjects representing virtual base classes are assigned more than once by the implicitly-defined copy/move assignment operator. [*Example 3*:

```cpp
struct V { };
struct A : virtual V { };
struct B : virtual V { };
struct C : B, A { };
```

It is unspecified whether the virtual base class subobject `V` is assigned twice by the implicitly-defined copy/move assignment operator for `C`. — *end example*]

**13** The implicitly-defined copy assignment operator for a union `X` copies the object representation ([[basic.types]](../basic/basic.types.md)) of `X`. If the source and destination of the assignment are not the same object, then for each object nested within ([[intro.object]](../basic/intro.object.md)) the object that is the source of the copy, a corresponding object *o* nested within the destination is created, and the lifetime of *o* begins before the copy is performed.

[^1]: Because a template assignment operator or an assignment operator taking an rvalue reference parameter is never a copy assignment operator, the presence of such an assignment operator does not suppress the implicit declaration of a copy assignment operator. Such assignment operators participate in overload resolution with other assignment operators, including copy assignment operators, and, if selected, will be used to assign an object.
[^2]: This implies that the reference parameter of the implicitly-declared copy assignment operator cannot bind to a `volatile` lvalue; see [[diff.class]](../diff/diff.class.md).
