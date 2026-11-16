# 10.4 Global module fragment [module.global.frag]

[Contents](../README.md) · up: [10 Modules \[module\]](module.md) · prev: [10.3 Import declaration \[module.import\]](module.import.md) · next: [10.5 Private module fragment \[module.private.frag\]](module.private.frag.md)

```bnf
global-module-fragment:
    module-keyword ';' declaration-seqₒₚₜ
```

**1** [*Note 1*: Prior to phase 4 of translation, only preprocessing directives can appear in the *declaration-seq* ([[cpp.pre]](../cpp/cpp.pre.md)). — *end note*]

**2** A *global-module-fragment* specifies the contents of the *global module fragment* for a module unit. The global module fragment can be used to provide declarations that are attached to the global module and usable within the module unit.

**3** A declaration *D* is *decl-reachable* from a declaration *S* in the same translation unit if:

- *D* does not declare a function or function template and *S* contains an *id-expression*, *namespace-name*, *type-name*, *template-name*, or *concept-name* naming *D*, or

- *D* declares a function or function template that is named by an expression ([[basic.def.odr]](../basic/basic.def.odr.md)) appearing in *S*, or

- *S* contains an expression `E` of the form

  ```bnf
  postfix-expression '(' expression-listₒₚₜ ')'
  ```

  whose *postfix-expression* denotes a dependent name, or for an operator expression whose operator denotes a dependent name, and *D* is found by name lookup for the corresponding name in an expression synthesized from `E` by replacing each type-dependent argument or operand with a value of a placeholder type with no associated namespaces or entities, or

- *S* contains an expression that takes the address of an overloaded function ([[over.over]](../over/over.over.md)) whose set of overloads contains *D* and for which the target type is dependent, or

- there exists a declaration *M* that is not a *namespace-definition* for which *M* is decl-reachable from *S* and either

  - *D* is decl-reachable from *M*, or
  - *D* redeclares the entity declared by *M* or *M* redeclares the entity declared by *D*, and *D* is neither a friend declaration nor a block-scope declaration, or
  - *D* declares a namespace *N* and *M* is a member of *N*, or
  - one of *M* and *D* declares a class or class template *C* and the other declares a member or friend of *C*, or
  - one of *D* and *M* declares an enumeration *E* and the other declares an enumerator of *E*, or
  - *D* declares a function or variable and *M* is declared in *D*,[^1] or
  - one of *M* and *D* declares a template and the other declares a partial or explicit specialization or an implicit or explicit instantiation of that template, or
  - one of *M* and *D* declares a class or enumeration type and the other introduces a typedef name for linkage purposes for that type.

In this determination, it is unspecified

- whether a reference to an *alias-declaration*, `typedef` declaration, *using-declaration*, or *namespace-alias-declaration* is replaced by the declarations they name prior to this determination,
- whether a *simple-template-id* that does not denote a dependent type and whose *template-name* names an alias template is replaced by its denoted type prior to this determination,
- whether a *decltype-specifier* that does not denote a dependent type is replaced by its denoted type prior to this determination, and
- whether a non-value-dependent constant expression is replaced by the result of constant evaluation prior to this determination.

**4** A declaration `D` in a global module fragment of a module unit is *discarded* if `D` is not decl-reachable from any *declaration* in the *declaration-seq* of the *translation-unit*. [*Note 2*: A discarded declaration is neither reachable nor visible to name lookup outside the module unit, nor in template instantiations whose points of instantiation ([[temp.point]](../temp/temp.point.md)) are outside the module unit, even when the instantiation context ([[module.context]](module.context.md)) includes the module unit. — *end note*]

**5** [*Example 1*:

```cpp
const int size = 2;
int ary1[size];                 // unspecified whether size is decl-reachable from ary1
constexpr int identity(int x) { return x; }
int ary2[identity(2)];          // unspecified whether identity is decl-reachable from ary2

template<typename> struct S;
template<typename, int> struct S2;
constexpr int g(int);

template<typename T, int N>
S<S2<T, g(N)>> f();             // S, S2, g, and :: are decl-reachable from f

template<int N>
void h() noexcept(g(N) == N);   // g and :: are decl-reachable from h
```

— *end example*]

**6** [*Example 2*:

*Source file `"foo.h"`:*

```cpp
namespace N {
  struct X {};
  int d();
  int e();
  inline int f(X, int = d()) { return e(); }
  int g(X);
  int h(X);
}
```

*Module `M` interface:*

```cpp
module;
#include "foo.h"
export module M;
template<typename T> int use_f() {
  N::X x;                       // N::X, N, and :: are decl-reachable from use_f
  return f(x, 123);             // N::f is decl-reachable from use_f,
                                // N::e is indirectly decl-reachable from use_f
                                //   because it is decl-reachable from N::f, and
                                // N::d is decl-reachable from use_f
                                //   because it is decl-reachable from N::f
                                //   even though it is not used in this call
}
template<typename T> int use_g() {
  N::X x;                       // N::X, N, and :: are decl-reachable from use_g
  return g((T(), x));           // N::g is not decl-reachable from use_g
}
template<typename T> int use_h() {
  N::X x;                       // N::X, N, and :: are decl-reachable from use_h
  return h((T(), x));           // N::h is not decl-reachable from use_h, but
                                // N::h is decl-reachable from use_h<int>
}
int k = use_h<int>();
  // use_h<int> is decl-reachable from k, so
  // N::h is decl-reachable from k
```

*Module `M` implementation:*

```cpp
module M;
int a = use_f<int>();           // OK
int b = use_g<int>();           // error: no viable function for call to g;
                                // g is not decl-reachable from purview of
                                // module M's interface, so is discarded
int c = use_h<int>();           // OK
```

— *end example*]

[^1]: A declaration can appear within a *lambda-expression* in the initializer of a variable.
