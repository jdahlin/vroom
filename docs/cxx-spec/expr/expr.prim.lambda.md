# 7.5.5 Lambda expressions [expr.prim.lambda]

[Contents](../README.md) · up: [7.5 Primary expressions \[expr.prim\]](expr.prim.md) · prev: [7.5.4.3 Destruction \[expr.prim.id.dtor\]](expr.prim.id.dtor.md) · next: [7.5.5.1 Closure types \[expr.prim.lambda.closure\]](expr.prim.lambda.closure.md)

```bnf
lambda-expression:
    lambda-introducer lambda-declaratorₒₚₜ compound-statement
    lambda-introducer '<' template-parameter-list '>' requires-clauseₒₚₜ lambda-declaratorₒₚₜ compound-statement
```

```bnf
lambda-introducer:
    '[' lambda-captureₒₚₜ ']'
```

```bnf
lambda-declarator:
    '(' parameter-declaration-clause ')' decl-specifier-seqₒₚₜ
        noexcept-specifierₒₚₜ attribute-specifier-seqₒₚₜ trailing-return-typeₒₚₜ requires-clauseₒₚₜ
```

**1** A *lambda-expression* provides a concise way to create a simple function object. [*Example 1*:

```cpp
#include <algorithm>
#include <cmath>
void abssort(float* x, unsigned N) {
  std::sort(x, x + N, [](float a, float b) { return std::abs(a) < std::abs(b); });
}
```

— *end example*]

**2** A *lambda-expression* is a prvalue whose result object is called the *closure object*. [*Note 1*: A closure object behaves like a function object ([[function.objects]](../lib/utilities/function.objects.md)). — *end note*]

**3** In the *decl-specifier-seq* of the *lambda-declarator*, each *decl-specifier* shall be one of `mutable`, `constexpr`, or `consteval`. [*Note 2*: The trailing *requires-clause* is described in [[dcl.decl]](../dcl/dcl.decl.md). — *end note*]

**4** If a *lambda-expression* does not include a *lambda-declarator*, it is as if the *lambda-declarator* were `()`. The lambda return type is `auto`, which is replaced by the type specified by the *trailing-return-type* if provided and/or deduced from `return` statements as described in [[dcl.spec.auto]](../dcl/dcl.spec.auto.md). [*Example 2*:

```cpp
auto x1 = [](int i){ return i; };       // OK: return type is int
auto x2 = []{ return { 1, 2 }; };       // error: deducing return type from braced-init-list
int j;
auto x3 = []()->auto&& { return j; };   // OK: return type is int&
```

— *end example*]

**5** A lambda is a *generic lambda* if the *lambda-expression* has any generic parameter type placeholders ([[dcl.spec.auto]](../dcl/dcl.spec.auto.md)), or if the lambda has a *template-parameter-list*. [*Example 3*:

```cpp
int i = [](int i, auto a) { return i; }(3, 4);                  // OK: a generic lambda
int j = []<class T>(T t, int i) { return i; }(3, 4);            // OK: a generic lambda
```

— *end example*]

## Subclauses

- [7.5.5.1 Closure types \[expr.prim.lambda.closure\]](expr.prim.lambda.closure.md)
- [7.5.5.2 Captures \[expr.prim.lambda.capture\]](expr.prim.lambda.capture.md)
