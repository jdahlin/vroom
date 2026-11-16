# 9.3.3.5 Functions [dcl.fct]

[Contents](README.md) · up: [9.3.3 Meaning of declarators \[dcl.meaning\]](dcl.meaning.md) · prev: [9.3.3.4 Arrays \[dcl.array\]](dcl.array.md) · next: [9.3.3.6 Default arguments \[dcl.fct.default\]](dcl.fct.default.md)

**1** In a declaration `T` `D` where `D` has the form

```bnf
'D1' '(' parameter-declaration-clause ')' cv-qualifier-seqₒₚₜ
    ref-qualifierₒₚₜ noexcept-specifierₒₚₜ attribute-specifier-seqₒₚₜ
```

and the type of the contained *declarator-id* in the declaration `T` `D1` is “*derived-declarator-type-list* `T`”, the type of the *declarator-id* in `D` is “*derived-declarator-type-list* `noexcept`ₒₚₜ function of parameter-type-list *cv-qualifier-seq*ₒₚₜ *ref-qualifier*ₒₚₜ returning `T`”, where

- the parameter-type-list is derived from the *parameter-declaration-clause* as described below and
- the optional `noexcept` is present if and only if the exception specification ([[except.spec]](except.spec.md)) is non-throwing.

The optional *attribute-specifier-seq* appertains to the function type.

**2** In a declaration `T` `D` where `D` has the form

```bnf
'D1' '(' parameter-declaration-clause ')' cv-qualifier-seqₒₚₜ
    ref-qualifierₒₚₜ noexcept-specifierₒₚₜ attribute-specifier-seqₒₚₜ trailing-return-type
```

and the type of the contained *declarator-id* in the declaration `T` `D1` is “*derived-declarator-type-list* `T`”, `T` shall be the single *type-specifier* `auto`. The type of the *declarator-id* in `D` is “*derived-declarator-type-list* `noexcept`ₒₚₜ function of parameter-type-list *cv-qualifier-seq*ₒₚₜ *ref-qualifier*ₒₚₜ returning `U`”, where

- the parameter-type-list is derived from the *parameter-declaration-clause* as described below,
- `U` is the type specified by the *trailing-return-type*, and
- the optional `noexcept` is present if and only if the exception specification is non-throwing.

The optional *attribute-specifier-seq* appertains to the function type.

**3** A type of either form is a *function type*.[^1]

```bnf
parameter-declaration-clause:
    parameter-declaration-listₒₚₜ '...'ₒₚₜ
    parameter-declaration-list ',' '...'
```

```bnf
parameter-declaration-list:
    parameter-declaration
    parameter-declaration-list ',' parameter-declaration
```

```bnf
parameter-declaration:
    attribute-specifier-seqₒₚₜ decl-specifier-seq declarator
    attribute-specifier-seqₒₚₜ decl-specifier-seq declarator '=' initializer-clause
    attribute-specifier-seqₒₚₜ decl-specifier-seq abstract-declaratorₒₚₜ
    attribute-specifier-seqₒₚₜ decl-specifier-seq abstract-declaratorₒₚₜ '=' initializer-clause
```

The optional *attribute-specifier-seq* in a *parameter-declaration* appertains to the parameter.

**4** The *parameter-declaration-clause* determines the arguments that can be specified, and their processing, when the function is called. [*Note 1*: The *parameter-declaration-clause* is used to convert the arguments specified on the function call; see [[expr.call]](expr.call.md). — *end note*] If the *parameter-declaration-clause* is empty, the function takes no arguments. A parameter list consisting of a single unnamed parameter of non-dependent type `void` is equivalent to an empty parameter list. Except for this special case, a parameter shall not have type *cv* `void`. A parameter with volatile-qualified type is deprecated; see [[depr.volatile.type]](depr.volatile.type.md). If the *parameter-declaration-clause* terminates with an ellipsis or a function parameter pack ([[temp.variadic]](temp.variadic.md)), the number of arguments shall be equal to or greater than the number of parameters that do not have a default argument and are not function parameter packs. Where syntactically correct and where “`...`” is not part of an *abstract-declarator*, “`, ...`” is synonymous with “`...`”. [*Example 1*: The declaration

```cpp
int printf(const char*, ...);
```

declares a function that can be called with varying numbers and types of arguments.

```cpp
printf("hello world");
printf("a=%d b=%d", a, b);
```

However, the first argument must be of a type that can be converted to a `const` `char*`. — *end example*] [*Note 2*: The standard header `<cstdarg>` ([[cstdarg.syn]](lib/cstdarg.syn.md)) contains a mechanism for accessing arguments passed using the ellipsis (see [[expr.call]](expr.call.md) and [[support.runtime]](lib/support.runtime.md)). — *end note*]

**5** The type of a function is determined using the following rules. The type of each parameter (including function parameter packs) is determined from its own *decl-specifier-seq* and *declarator*. After determining the type of each parameter, any parameter of type “array of `T`” or of function type `T` is adjusted to be “pointer to `T`”. After producing the list of parameter types, any top-level *cv-qualifier*s modifying a parameter type are deleted when forming the function type. The resulting list of transformed parameter types and the presence or absence of the ellipsis or a function parameter pack is the function’s *parameter-type-list*. [*Note 3*: This transformation does not affect the types of the parameters. For example, `int(*)(const int p, decltype(p)*)` and `int(*)(int, const int*)` are identical types. — *end note*]

**6** A function type with a *cv-qualifier-seq* or a *ref-qualifier* (including a type named by *typedef-name* ([[dcl.typedef]](dcl.typedef.md), [[temp.param]](temp.param.md))) shall appear only as:

- the function type for a non-static member function,
- the function type to which a pointer to member refers,
- the top-level function type of a function typedef declaration or *alias-declaration*,
- the *type-id* in the default argument of a *type-parameter* ([[temp.param]](temp.param.md)), or
- the *type-id* of a *template-argument* for a *type-parameter* ([[temp.arg.type]](temp.arg.type.md)).

[*Example 2*:

```cpp
typedef int FIC(int) const;
FIC f;              // error: does not declare a member function
struct S {
  FIC f;            // OK
};
FIC S::*pm = &S::f; // OK
```

— *end example*]

**7** The effect of a *cv-qualifier-seq* in a function declarator is not the same as adding cv-qualification on top of the function type. In the latter case, the cv-qualifiers are ignored. [*Note 4*: A function type that has a *cv-qualifier-seq* is not a cv-qualified type; there are no cv-qualified function types. — *end note*] [*Example 3*:

```cpp
typedef void F();
struct S {
  const F f;        // OK: equivalent to: void f();
};
```

— *end example*]

**8** The return type, the parameter-type-list, the *ref-qualifier*, the *cv-qualifier-seq*, and the exception specification, but not the default arguments ([[dcl.fct.default]](dcl.fct.default.md)) or the trailing *requires-clause* ([[dcl.decl]](dcl.decl.md)), are part of the function type. [*Note 5*: Function types are checked during the assignments and initializations of pointers to functions, references to functions, and pointers to member functions. — *end note*]

**9** [*Example 4*: The declaration

```cpp
int fseek(FILE*, long, int);
```

declares a function taking three arguments of the specified types, and returning `int` ([[dcl.type]](dcl.type.md)). — *end example*]

**10** A single name can be used for several different functions in a single scope; this is function overloading ([[over]](over.md)). All declarations for a function shall have equivalent return types, parameter-type-lists, and *requires-clause*s ([[temp.over.link]](temp.over.link.md)).

**11** Functions shall not have a return type of type array or function, although they may have a return type of type pointer or reference to such things. There shall be no arrays of functions, although there can be arrays of pointers to functions.

**12** A volatile-qualified return type is deprecated; see [[depr.volatile.type]](depr.volatile.type.md).

**13** Types shall not be defined in return or parameter types.

**14** A typedef of function type may be used to declare a function but shall not be used to define a function ([[dcl.fct.def]](dcl.fct.def.md)). [*Example 5*:

```cpp
typedef void F();
F  fv;              // OK: equivalent to void fv();
F  fv { }           // error
void fv() { }       // OK: definition of fv
```

— *end example*]

**15** An identifier can optionally be provided as a parameter name; if present in a function definition ([[dcl.fct.def]](dcl.fct.def.md)), it names a parameter. [*Note 6*: In particular, parameter names are also optional in function definitions and names used for a parameter in different declarations and the definition of a function need not be the same. If a parameter name is present in a function declaration that is not a definition, it cannot be used outside of its function declarator because that is the extent of its potential scope ([[basic.scope.param]](basic.scope.param.md)). — *end note*]

**16** [*Example 6*: The declaration

```cpp
int i,
    *pi,
    f(),
    *fpi(int),
    (*pif)(const char*, const char*),
    (*fpif(int))(int);
```

declares an integer `i`, a pointer `pi` to an integer, a function `f` taking no arguments and returning an integer, a function `fpi` taking an integer argument and returning a pointer to an integer, a pointer `pif` to a function which takes two pointers to constant characters and returns an integer, a function `fpif` taking an integer argument and returning a pointer to a function that takes an integer argument and returns an integer. It is especially useful to compare `fpi` and `pif`. The binding of `*fpi(int)` is `*(fpi(int))`, so the declaration suggests, and the same construction in an expression requires, the calling of a function `fpi`, and then using indirection through the (pointer) result to yield an integer. In the declarator `(*pif)(const char*, const char*)`, the extra parentheses are necessary to indicate that indirection through a pointer to a function yields a function, which is then called. — *end example*] [*Note 7*: Typedefs and *trailing-return-type*s are sometimes convenient when the return type of a function is complex. For example, the function `fpif` above could have been declared

```cpp
typedef int  IFUNC(int);
IFUNC*  fpif(int);
```

or

```cpp
auto fpif(int)->int(*)(int);
```

A *trailing-return-type* is most useful for a type that would be more complicated to specify before the *declarator-id*:

```cpp
template <class T, class U> auto add(T t, U u) -> decltype(t + u);
```

rather than

```cpp
template <class T, class U> decltype((*(T*)0) + (*(U*)0)) add(T t, U u);
```

— *end note*]

**17** A *non-template function* is a function that is not a function template specialization. [*Note 8*: A function template is not a function. — *end note*]

**18** An *abbreviated function template* is a function declaration that has one or more generic parameter type placeholders ([[dcl.spec.auto]](dcl.spec.auto.md)). An abbreviated function template is equivalent to a function template ([[temp.fct]](temp.fct.md)) whose *template-parameter-list* includes one invented type *template-parameter* for each generic parameter type placeholder of the function declaration, in order of appearance. For a *placeholder-type-specifier* of the form `auto`, the invented parameter is an unconstrained *type-parameter*. For a *placeholder-type-specifier* of the form *type-constraint* `auto`, the invented parameter is a *type-parameter* with that *type-constraint*. The invented type *template-parameter* is a template parameter pack if the corresponding *parameter-declaration* declares a function parameter pack ([[dcl.fct]](dcl.fct.md)). If the placeholder contains `decltype(auto)`, the program is ill-formed. The adjusted function parameters of an abbreviated function template are derived from the *parameter-declaration-clause* by replacing each occurrence of a placeholder with the name of the corresponding invented *template-parameter*. [*Example 7*:

```cpp
template<typename T>     concept C1 = /* ... */;
template<typename T>     concept C2 = /* ... */;
template<typename... Ts> concept C3 = /* ... */;

void g1(const C1 auto*, C2 auto&);
void g2(C1 auto&...);
void g3(C3 auto...);
void g4(C3 auto);
```

These declarations are functionally equivalent (but not equivalent) to the following declarations.

```cpp
template<C1 T, C2 U> void g1(const T*, U&);
template<C1... Ts>   void g2(Ts&...);
template<C3... Ts>   void g3(Ts...);
template<C3 T>       void g4(T);
```

Abbreviated function templates can be specialized like all function templates.

```cpp
template<> void g1<int>(const int*, const double&); // OK, specialization of g1<int, const double>
```

— *end example*]

**19** An abbreviated function template can have a *template-head*. The invented *template-parameters* are appended to the *template-parameter-list* after the explicitly declared *template-parameters*. [*Example 8*:

```cpp
template<typename> concept C = /* ... */;

template <typename T, C U>
  void g(T x, U y, C auto z);
```

This is functionally equivalent to each of the following two declarations.

```cpp
template<typename T, C U, C W>
  void g(T x, U y, W z);

template<typename T, typename U, typename W>
  requires C<U> && C<W>
  void g(T x, U y, W z);
```

— *end example*]

**20** A function declaration at block scope shall not declare an abbreviated function template.

**21** A *declarator-id* or *abstract-declarator* containing an ellipsis shall only be used in a *parameter-declaration*. When it is part of a *parameter-declaration-clause*, the *parameter-declaration* declares a function parameter pack ([[temp.variadic]](temp.variadic.md)). Otherwise, the *parameter-declaration* is part of a *template-parameter-list* and declares a template parameter pack; see [[temp.param]](temp.param.md). A function parameter pack is a pack expansion ([[temp.variadic]](temp.variadic.md)). [*Example 9*:

```cpp
template<typename... T> void f(T (* ...t)(int, int));

int add(int, int);
float subtract(int, int);

void g() {
  f(add, subtract);
}
```

— *end example*]

**22** There is a syntactic ambiguity when an ellipsis occurs at the end of a *parameter-declaration-clause* without a preceding comma. In this case, the ellipsis is parsed as part of the *abstract-declarator* if the type of the parameter either names a template parameter pack that has not been expanded or contains `auto`; otherwise, it is parsed as part of the *parameter-declaration-clause*.[^2]

[^1]: As indicated by syntax, cv-qualifiers are a significant component in function return types.
[^2]: One can explicitly disambiguate the parse either by introducing a comma (so the ellipsis will be parsed as part of the *parameter-declaration-clause*) or by introducing a name for the parameter (so the ellipsis will be parsed as part of the *declarator-id*).
