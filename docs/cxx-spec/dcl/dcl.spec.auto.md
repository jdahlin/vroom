# 9.2.8.5 Placeholder type specifiers [dcl.spec.auto]

[Contents](../README.md) · up: [9.2.8 Type specifiers \[dcl.type\]](dcl.type.md) · prev: [9.2.8.4 Decltype specifiers \[dcl.type.decltype\]](dcl.type.decltype.md) · next: [9.2.8.5.1 Placeholder type deduction \[dcl.type.auto.deduct\]](dcl.type.auto.deduct.md)

```bnf
placeholder-type-specifier:
  type-constraintₒₚₜ 'auto'
  type-constraintₒₚₜ 'decltype' '(' 'auto' ')'
```

**1** A *placeholder-type-specifier* designates a placeholder type that will be replaced later by deduction from an initializer.

**2** A *placeholder-type-specifier* of the form *type-constraint*ₒₚₜ `auto` can be used as a *decl-specifier* of the *decl-specifier-seq* of a *parameter-declaration* of a function declaration or *lambda-expression* and, if it is not the `auto` *type-specifier* introducing a *trailing-return-type* (see below), is a *generic parameter type placeholder* of the function declaration or *lambda-expression*. [*Note 1*: Having a generic parameter type placeholder signifies that the function is an abbreviated function template ([[dcl.fct]](dcl.fct.md)) or the lambda is a generic lambda ([[expr.prim.lambda]](../expr/expr.prim.lambda.md)). — *end note*]

**3** The placeholder type can appear with a function declarator in the *decl-specifier-seq*, *type-specifier-seq*, *conversion-function-id*, or *trailing-return-type*, in any context where such a declarator is valid. If the function declarator includes a *trailing-return-type* ([[dcl.fct]](dcl.fct.md)), that *trailing-return-type* specifies the declared return type of the function. Otherwise, the function declarator shall declare a function. If the declared return type of the function contains a placeholder type, the return type of the function is deduced from non-discarded `return` statements, if any, in the body of the function ([[stmt.if]](../stmt/stmt.if.md)).

**4** The type of a variable declared using a placeholder type is deduced from its initializer. This use is allowed in an initializing declaration ([[dcl.init]](dcl.init.md)) of a variable. The placeholder type shall appear as one of the *decl-specifier*s in the *decl-specifier-seq* and the *decl-specifier-seq* shall be followed by one or more *declarator*s, each of which shall be followed by a non-empty *initializer*. In an *initializer* of the form

```cpp
( expression-list )
```

the *expression-list* shall be a single *assignment-expression*. [*Example 1*:

```cpp
auto x = 5;                     // OK: x has type int
const auto *v = &x, u = 6;      // OK: v has type const int*, u has type const int
static auto y = 0.0;            // OK: y has type double
auto int r;                     // error: auto is not a storage-class-specifier
auto f() -> int;                // OK: f returns int
auto g() { return 0.0; }        // OK: g returns double
auto h();                       // OK: h's return type will be deduced when it is defined
```

— *end example*] The `auto` *type-specifier* can also be used to introduce a structured binding declaration ([[dcl.struct.bind]](dcl.struct.bind.md)).

**5** A placeholder type can also be used in the *type-specifier-seq* in the *new-type-id* or *type-id* of a *new-expression* ([[expr.new]](../expr/expr.new.md)) and as a *decl-specifier* of the *parameter-declaration*’s *decl-specifier-seq* in a *template-parameter* ([[temp.param]](../temp/temp.param.md)).

**6** A program that uses a placeholder type in a context not explicitly allowed in this subclause is ill-formed.

**7** If the *init-declarator-list* contains more than one *init-declarator*, they shall all form declarations of variables. The type of each declared variable is determined by placeholder type deduction ([[dcl.type.auto.deduct]](dcl.type.auto.deduct.md)), and if the type that replaces the placeholder type is not the same in each deduction, the program is ill-formed.

[*Example 2*:

```cpp
auto x = 5, *y = &x;            // OK: auto is int
auto a = 5, b = { 1, 2 };       // error: different types for auto
```

— *end example*]

**8** If a function with a declared return type that contains a placeholder type has multiple non-discarded `return` statements, the return type is deduced for each such `return` statement. If the type deduced is not the same in each deduction, the program is ill-formed.

**9** If a function with a declared return type that uses a placeholder type has no non-discarded `return` statements, the return type is deduced as though from a `return` statement with no operand at the closing brace of the function body. [*Example 3*:

```cpp
auto  f() { }                   // OK, return type is void
auto* g() { }                   // error: cannot deduce auto* from void()
```

— *end example*]

**10** An exported function with a declared return type that uses a placeholder type shall be defined in the translation unit containing its exported declaration, outside the *private-module-fragment* (if any). [*Note 2*: The deduced return type cannot have a name with internal linkage ([[basic.link]](../basic/basic.link.md)). — *end note*]

**11** If the name of an entity with an undeduced placeholder type appears in an expression, the program is ill-formed. Once a non-discarded `return` statement has been seen in a function, however, the return type deduced from that statement can be used in the rest of the function, including in other `return` statements. [*Example 4*:

```cpp
auto n = n;                     // error: n's initializer refers to n
auto f();
void g() { &f; }                // error: f's return type is unknown
auto sum(int i) {
  if (i == 1)
    return i;                   // sum's return type is int
  else
    return sum(i-1)+i;          // OK, sum's return type has been deduced
}
```

— *end example*]

**12** Return type deduction for a templated entity that is a function or function template with a placeholder in its declared type occurs when the definition is instantiated even if the function body contains a `return` statement with a non-type-dependent operand. [*Note 3*: Therefore, any use of a specialization of the function template will cause an implicit instantiation. Any errors that arise from this instantiation are not in the immediate context of the function type and can result in the program being ill-formed ([[temp.deduct]](../temp/temp.deduct.md)). — *end note*] [*Example 5*:

```cpp
template <class T> auto f(T t) { return t; }    // return type deduced at instantiation time
typedef decltype(f(1)) fint_t;                  // instantiates f<int> to deduce return type
template<class T> auto f(T* t) { return *t; }
void g() { int (*p)(int*) = &f; }               // instantiates both fs to determine return types,
                                                // chooses second
```

— *end example*]

**13** Redeclarations or specializations of a function or function template with a declared return type that uses a placeholder type shall also use that placeholder, not a deduced type. Similarly, redeclarations or specializations of a function or function template with a declared return type that does not use a placeholder type shall not use a placeholder. [*Example 6*:

```cpp
auto f();
auto f() { return 42; }                         // return type is int
auto f();                                       // OK
int f();                                        // error: cannot be overloaded with auto f()
decltype(auto) f();                             // error: auto and decltype(auto) don't match

template <typename T> auto g(T t) { return t; } // #1
template auto g(int);                           // OK, return type is int
template char g(char);                          // error: no matching template
template<> auto g(double);                      // OK, forward declaration with unknown return type

template <class T> T g(T t) { return t; }       // OK, not functionally equivalent to #1
template char g(char);                          // OK, now there is a matching template
template auto g(float);                         // still matches #1

void h() { return g(42); }                      // error: ambiguous

template <typename T> struct A {
  friend T frf(T);
};
auto frf(int i) { return i; }                   // not a friend of A<int>
extern int v;
auto v = 17;                                    // OK, redeclares v
struct S {
  static int i;
};
auto S::i = 23;                                 // OK
```

— *end example*]

**14** A function declared with a return type that uses a placeholder type shall not be `virtual` ([[class.virtual]](../class/class.virtual.md)).

**15** A function declared with a return type that uses a placeholder type shall not be a coroutine ([[dcl.fct.def.coroutine]](dcl.fct.def.coroutine.md)).

**16** An explicit instantiation declaration ([[temp.explicit]](../temp/temp.explicit.md)) does not cause the instantiation of an entity declared using a placeholder type, but it also does not prevent that entity from being instantiated as needed to determine its type. [*Example 7*:

```cpp
template <typename T> auto f(T t) { return t; }
extern template auto f(int);    // does not instantiate f<int>
int (*p)(int) = f;              // instantiates f<int> to determine its return type, but an explicit
                                // instantiation definition is still required somewhere in the program
```

— *end example*]

## Subclauses

- [9.2.8.5.1 Placeholder type deduction \[dcl.type.auto.deduct\]](dcl.type.auto.deduct.md)
