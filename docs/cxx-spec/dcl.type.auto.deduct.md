# 9.2.8.5.1 Placeholder type deduction [dcl.type.auto.deduct]

[Contents](README.md) · up: [9.2.8.5 Placeholder type specifiers \[dcl.spec.auto\]](dcl.spec.auto.md) · prev: [9.2.8.5 Placeholder type specifiers \[dcl.spec.auto\]](dcl.spec.auto.md) · next: [9.2.8.6 Deduced class template specialization types \[dcl.type.class.deduct\]](dcl.type.class.deduct.md)

**1** *Placeholder type deduction* is the process by which a type containing a placeholder type is replaced by a deduced type.

**2** A type `T` containing a placeholder type, and a corresponding initializer *E*, are determined as follows:

- for a non-discarded `return` statement that occurs in a function declared with a return type that contains a placeholder type, `T` is the declared return type and *E* is the operand of the `return` statement. If the `return` statement has no operand, then *E* is `void()`;
- for a variable declared with a type that contains a placeholder type, `T` is the declared type of the variable and *E* is the initializer. If the initialization is direct-list-initialization, the initializer shall be a *braced-init-list* containing only a single *assignment-expression* and *E* is the *assignment-expression*;
- for a non-type template parameter declared with a type that contains a placeholder type, `T` is the declared type of the non-type template parameter and *E* is the corresponding template argument.

In the case of a `return` statement with no operand or with an operand of type `void`, `T` shall be either *type-constraint*ₒₚₜ `decltype(auto)` or *cv* *type-constraint*ₒₚₜ `auto`.

**3** If the deduction is for a `return` statement and *E* is a *braced-init-list* ([[dcl.init.list]](dcl.init.list.md)), the program is ill-formed.

**4** If the *placeholder-type-specifier* is of the form *type-constraint*ₒₚₜ `auto`, the deduced type `T`' replacing `T` is determined using the rules for template argument deduction. Obtain `P` from `T` by replacing the occurrences of *type-constraint*ₒₚₜ `auto` either with a new invented type template parameter `U` or, if the initialization is copy-list-initialization, with `std::initializer_list<U>`. Deduce a value for `U` using the rules of template argument deduction from a function call ([[temp.deduct.call]](temp.deduct.call.md)), where `P` is a function template parameter type and the corresponding argument is *E*. If the deduction fails, the declaration is ill-formed. Otherwise, `T`' is obtained by substituting the deduced `U` into `P`. [*Example 1*:

```cpp
auto x1 = { 1, 2 };             // decltype(x1) is std::initializer_list<int>
auto x2 = { 1, 2.0 };           // error: cannot deduce element type
auto x3{ 1, 2 };                // error: not a single element
auto x4 = { 3 };                // decltype(x4) is std::initializer_list<int>
auto x5{ 3 };                   // decltype(x5) is int
```

— *end example*]

[*Example 2*:

```cpp
const auto &i = expr;
```

The type of `i` is the deduced type of the parameter `u` in the call `f(expr)` of the following invented function template:

```cpp
template <class U> void f(const U& u);
```

— *end example*]

**5** If the *placeholder-type-specifier* is of the form *type-constraint*ₒₚₜ `decltype(auto)`, `T` shall be the placeholder alone. The type deduced for `T` is determined as described in [[dcl.type.simple]](dcl.type.simple.md), as though *E* had been the operand of the `decltype`. [*Example 3*:

```cpp
int i;
int&& f();
auto           x2a(i);          // decltype(x2a) is int
decltype(auto) x2d(i);          // decltype(x2d) is int
auto           x3a = i;         // decltype(x3a) is int
decltype(auto) x3d = i;         // decltype(x3d) is int
auto           x4a = (i);       // decltype(x4a) is int
decltype(auto) x4d = (i);       // decltype(x4d) is int&
auto           x5a = f();       // decltype(x5a) is int
decltype(auto) x5d = f();       // decltype(x5d) is int&&
auto           x6a = { 1, 2 };  // decltype(x6a) is std::initializer_list<int>
decltype(auto) x6d = { 1, 2 };  // error: { 1, 2 } is not an expression
auto          *x7a = &i;        // decltype(x7a) is int*
decltype(auto)*x7d = &i;        // error: declared type is not plain decltype(auto)
```

— *end example*]

**6** For a *placeholder-type-specifier* with a *type-constraint*, the immediately-declared constraint ([[temp.param]](temp.param.md)) of the *type-constraint* for the type deduced for the placeholder shall be satisfied.
