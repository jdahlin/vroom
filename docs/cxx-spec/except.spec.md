# 14.5 Exception specifications [except.spec]

[Contents](README.md) · up: [14 Exception handling \[except\]](except.md) · prev: [14.4 Handling an exception \[except.handle\]](except.handle.md) · next: [14.6 Special functions \[except.special\]](except.special.md)

**1** The predicate indicating whether a function cannot exit via an exception is called the *exception specification* of the function. If the predicate is false, the function has a *potentially-throwing exception specification*, otherwise it has a *non-throwing exception specification*. The exception specification is either defined implicitly, or defined explicitly by using a *noexcept-specifier* as a suffix of a function declarator ([[dcl.fct]](dcl.fct.md)).

```bnf
noexcept-specifier:
    'noexcept' '(' constant-expression ')'
    'noexcept'
```

**2** In a *noexcept-specifier*, the *constant-expression*, if supplied, shall be a contextually converted constant expression of type `bool` ([[expr.const]](expr.const.md)); that constant expression is the exception specification of the function type in which the *noexcept-specifier* appears. A `(` token that follows `noexcept` is part of the *noexcept-specifier* and does not commence an initializer ([[dcl.init]](dcl.init.md)). The *noexcept-specifier* `noexcept` without a *constant-expression* is equivalent to the *noexcept-specifier* `noexcept(true)`.

**3** If a declaration of a function does not have a *noexcept-specifier*, the declaration has a potentially throwing exception specification unless it is a destructor or a deallocation function or is defaulted on its first declaration, in which cases the exception specification is as specified below and no other declaration for that function shall have a *noexcept-specifier*. In an explicit instantiation ([[temp.explicit]](temp.explicit.md)) a *noexcept-specifier* may be specified, but is not required. If a *noexcept-specifier* is specified in an explicit instantiation directive, the exception specification shall be the same as the exception specification of all other declarations of that function. A diagnostic is required only if the exception specifications are not the same within a single translation unit.

**4** If a virtual function has a non-throwing exception specification, all declarations, including the definition, of any function that overrides that virtual function in any derived class shall have a non-throwing exception specification, unless the overriding function is defined as deleted. [*Example 1*:

```cpp
struct B {
  virtual void f() noexcept;
  virtual void g();
  virtual void h() noexcept = delete;
};

struct D: B {
  void f();                     // error
  void g() noexcept;            // OK
  void h() = delete;            // OK
};
```

The declaration of `D::f` is ill-formed because it has a potentially-throwing exception specification, whereas `B::f` has a non-throwing exception specification. — *end example*]

**5** Whenever an exception is thrown and the search for a handler ([[except.handle]](except.handle.md)) encounters the outermost block of a function with a non-throwing exception specification, the function `std::terminate` is called ([[except.terminate]](except.terminate.md)). [*Note 1*: An implementation is not permitted to reject an expression merely because, when executed, it throws or might throw an exception from a function with a non-throwing exception specification. — *end note*] [*Example 2*:

```cpp
extern void f();                // potentially-throwing

void g() noexcept {
  f();                          // valid, even if f throws
  throw 42;                     // valid, effectively a call to std::terminate
}
```

The call to `f` is well-formed even though, when called, `f` might throw an exception. — *end example*]

**6** An expression *E* is *potentially-throwing* if

- *E* is a function call ([[expr.call]](expr.call.md)) whose *postfix-expression* has a function type, or a pointer-to-function type, with a potentially-throwing exception specification, or
- *E* implicitly invokes a function (such as an overloaded operator, an allocation function in a *new-expression*, a constructor for a function argument, or a destructor if *E* is a full-expression ([[intro.execution]](intro.execution.md))) that is potentially-throwing, or
- *E* is a *throw-expression* ([[expr.throw]](expr.throw.md)), or
- *E* is a `dynamic_cast` expression that casts to a reference type and requires a runtime check ([[expr.dynamic.cast]](expr.dynamic.cast.md)), or
- *E* is a `typeid` expression applied to a (possibly parenthesized) built-in unary `*` operator applied to a pointer to a polymorphic class type ([[expr.typeid]](expr.typeid.md)), or
- any of the immediate subexpressions ([[intro.execution]](intro.execution.md)) of *E* is potentially-throwing.

**7** An implicitly-declared constructor for a class `X`, or a constructor without a *noexcept-specifier* that is defaulted on its first declaration, has a potentially-throwing exception specification if and only if any of the following constructs is potentially-throwing:

- a constructor selected by overload resolution in the implicit definition of the constructor for class `X` to initialize a potentially constructed subobject, or
- a subexpression of such an initialization, such as a default argument expression, or,
- for a default constructor, a default member initializer.

[*Note 2*: Even though destructors for fully-constructed subobjects are invoked when an exception is thrown during the execution of a constructor ([[except.ctor]](except.ctor.md)), their exception specifications do not contribute to the exception specification of the constructor, because an exception thrown from such a destructor would call the function `std::terminate` rather than escape the constructor ([[except.throw]](except.throw.md), [[except.terminate]](except.terminate.md)). — *end note*]

**8** The exception specification for an implicitly-declared destructor, or a destructor without a *noexcept-specifier*, is potentially-throwing if and only if any of the destructors for any of its potentially constructed subobjects is potentially-throwing or the destructor is virtual and the destructor of any virtual base class is potentially-throwing.

**9** The exception specification for an implicitly-declared assignment operator, or an assignment-operator without a *noexcept-specifier* that is defaulted on its first declaration, is potentially-throwing if and only if the invocation of any assignment operator in the implicit definition is potentially-throwing.

**10** A deallocation function ([[basic.stc.dynamic.deallocation]](basic.stc.dynamic.deallocation.md)) with no explicit *noexcept-specifier* has a non-throwing exception specification.

**11** The exception specification for a comparison operator function ([[over.binary]](over.binary.md)) without a *noexcept-specifier* that is defaulted on its first declaration is potentially-throwing if and only if any expression in the implicit definition is potentially-throwing.

**12** [*Example 3*:

```cpp
struct A {
  A(int = (A(5), 0)) noexcept;
  A(const A&) noexcept;
  A(A&&) noexcept;
  ~A();
};
struct B {
  B() noexcept;
  B(const B&) = default;        // implicit exception specification is noexcept(true)
  B(B&&, int = (throw 42, 0)) noexcept;
  ~B() noexcept(false);
};
int n = 7;
struct D : public A, public B {
    int * p = new int[n];
    // D::D() potentially-throwing, as the new operator may throw bad_alloc or bad_array_new_length
    // D::D(const D&) non-throwing
    // D::D(D&&) potentially-throwing, as the default argument for B's constructor may throw
    // D::~D() potentially-throwing
};
```

Furthermore, if `A::~A()` were virtual, the program would be ill-formed since a function that overrides a virtual function from a base class shall not have a potentially-throwing exception specification if the base class function has a non-throwing exception specification. — *end example*]

**13** An exception specification is considered to be *needed* when:

- in an expression, the function is the unique lookup result or the selected member of a set of overloaded functions ([[basic.lookup]](basic.lookup.md), [[over.match]](over.match.md), [[over.over]](over.over.md));
- the function is odr-used ([[basic.def.odr]](basic.def.odr.md)) or, if it appears in an unevaluated operand, would be odr-used if the expression were potentially-evaluated;
- the exception specification is compared to that of another declaration (e.g., an explicit specialization or an overriding virtual function);
- the function is defined; or
- the exception specification is needed for a defaulted function that calls the function. [*Note 3*: A defaulted declaration does not require the exception specification of a base member function to be evaluated until the implicit exception specification of the derived function is needed, but an explicit *noexcept-specifier* needs the implicit exception specification to compare against. — *end note*]

The exception specification of a defaulted function is evaluated as described above only when needed; similarly, the *noexcept-specifier* of a specialization of a function template or member function of a class template is instantiated only when needed.
