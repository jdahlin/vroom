# 7.5.5.1 Closure types [expr.prim.lambda.closure]

[Contents](README.md) · up: [7.5.5 Lambda expressions \[expr.prim.lambda\]](expr.prim.lambda.md) · prev: [7.5.5 Lambda expressions \[expr.prim.lambda\]](expr.prim.lambda.md) · next: [7.5.5.2 Captures \[expr.prim.lambda.capture\]](expr.prim.lambda.capture.md)

**1** The type of a *lambda-expression* (which is also the type of the closure object) is a unique, unnamed non-union class type, called the *closure type*, whose properties are described below.

**2** The closure type is declared in the smallest block scope, class scope, or namespace scope that contains the corresponding *lambda-expression*. [*Note 1*: This determines the set of namespaces and classes associated with the closure type ([[basic.lookup.argdep]](basic.lookup.argdep.md)). The parameter types of a *lambda-declarator* do not affect these associated namespaces and classes. — *end note*] The closure type is not an aggregate type ([[dcl.init.aggr]](dcl.init.aggr.md)). An implementation may define the closure type differently from what is described below provided this does not alter the observable behavior of the program other than by changing:

- the size and/or alignment of the closure type,
- whether the closure type is trivially copyable ([[class.prop]](class.prop.md)), or
- whether the closure type is a standard-layout class ([[class.prop]](class.prop.md)).

An implementation shall not add members of rvalue reference type to the closure type.

**3** The closure type for a *lambda-expression* has a public inline function call operator (for a non-generic lambda) or function call operator template (for a generic lambda) ([[over.call]](over.call.md)) whose parameters and return type are described by the *lambda-expression*’s *parameter-declaration-clause* and *trailing-return-type* respectively, and whose *template-parameter-list* consists of the specified *template-parameter-list*, if any. The *requires-clause* of the function call operator template is the *requires-clause* immediately following `<` *template-parameter-list* `>`, if any. The trailing *requires-clause* of the function call operator or operator template is the *requires-clause* of the *lambda-declarator*, if any. [*Note 2*: The function call operator template for a generic lambda might be an abbreviated function template ([[dcl.fct]](dcl.fct.md)). — *end note*] [*Example 1*:

```cpp
auto glambda = [](auto a, auto&& b) { return a < b; };
bool b = glambda(3, 3.14);                                      // OK

auto vglambda = [](auto printer) {
  return [=](auto&& ... ts) {                                   // OK: ts is a function parameter pack
    printer(std::forward<decltype(ts)>(ts)...);

    return [=]() {
      printer(ts ...);
    };
  };
};
auto p = vglambda( [](auto v1, auto v2, auto v3)
                   { std::cout << v1 << v2 << v3; } );
auto q = p(1, 'a', 3.14);                                       // OK: outputs 1a3.14
q();                                                            // OK: outputs 1a3.14
```

— *end example*]

**4** The function call operator or operator template is declared `const` ([[class.mfct.non-static]](class.mfct.non-static.md)) if and only if the *lambda-expression*’s *parameter-declaration-clause* is not followed by `mutable`. It is neither virtual nor declared `volatile`. Any *noexcept-specifier* specified on a *lambda-expression* applies to the corresponding function call operator or operator template. An *attribute-specifier-seq* in a *lambda-declarator* appertains to the type of the corresponding function call operator or operator template. The function call operator or any given operator template specialization is a constexpr function if either the corresponding *lambda-expression*’s *parameter-declaration-clause* is followed by `constexpr` or `consteval`, or it satisfies the requirements for a constexpr function ([[dcl.constexpr]](dcl.constexpr.md)). It is an immediate function ([[dcl.constexpr]](dcl.constexpr.md)) if the corresponding *lambda-expression*’s *parameter-declaration-clause* is followed by `consteval`. [*Note 3*: Names referenced in the *lambda-declarator* are looked up in the context in which the *lambda-expression* appears. — *end note*] [*Example 2*:

```cpp
auto ID = [](auto a) { return a; };
static_assert(ID(3) == 3);                      // OK

struct NonLiteral {
  NonLiteral(int n) : n(n) { }
  int n;
};
static_assert(ID(NonLiteral{3}).n == 3);        // error
```

— *end example*]

**5** [*Example 3*:

```cpp
auto monoid = [](auto v) { return [=] { return v; }; };
auto add = [](auto m1) constexpr {
  auto ret = m1();
  return [=](auto m2) mutable {
    auto m1val = m1();
    auto plus = [=](auto m2val) mutable constexpr
                   { return m1val += m2val; };
    ret = plus(m2());
    return monoid(ret);
  };
};
constexpr auto zero = monoid(0);
constexpr auto one = monoid(1);
static_assert(add(one)(zero)() == one());       // OK

// Since two below is not declared constexpr, an evaluation of its constexpr member function call operator
// cannot perform an lvalue-to-rvalue conversion on one of its subobjects (that represents its capture)
// in a constant expression.
auto two = monoid(2);
assert(two() == 2); // OK, not a constant expression.
static_assert(add(one)(one)() == two());        // error: two() is not a constant expression
static_assert(add(one)(one)() == monoid(2)());  // OK
```

— *end example*]

**6** [*Note 4*: The function call operator or operator template may be constrained ([[temp.constr.decl]](temp.constr.decl.md)) by a *type-constraint* ([[temp.param]](temp.param.md)), a *requires-clause* ([[temp.pre]](temp.pre.md)), or a trailing *requires-clause* ([[dcl.decl]](dcl.decl.md)). [*Example 4*:

```cpp
template <typename T> concept C1 = /* ... */;
template <std::size_t N> concept C2 = /* ... */;
template <typename A, typename B> concept C3 = /* ... */;

auto f = []<typename T1, C1 T2> requires C2<sizeof(T1) + sizeof(T2)>
         (T1 a1, T1 b1, T2 a2, auto a3, auto a4) requires C3<decltype(a4), T2> {
  // T2 is constrained by a type-constraint.
  // T1 and T2 are constrained by a requires-clause, and
  // T2 and the type of a4 are constrained by a trailing requires-clause.
};
```

— *end example*] — *end note*]

**7** The closure type for a non-generic *lambda-expression* with no *lambda-capture* whose constraints (if any) are satisfied has a conversion function to pointer to function with C++ language linkage ([[dcl.link]](dcl.link.md)) having the same parameter and return types as the closure type’s function call operator. The conversion is to “pointer to `noexcept` function” if the function call operator has a non-throwing exception specification. The value returned by this conversion function is the address of a function `F` that, when invoked, has the same effect as invoking the closure type’s function call operator on a default-constructed instance of the closure type. `F` is a constexpr function if the function call operator is a constexpr function and is an immediate function if the function call operator is an immediate function.

**8** For a generic lambda with no *lambda-capture*, the closure type has a conversion function template to pointer to function. The conversion function template has the same invented template parameter list, and the pointer to function has the same parameter types, as the function call operator template. The return type of the pointer to function shall behave as if it were a *decltype-specifier* denoting the return type of the corresponding function call operator template specialization.

**9** [*Note 5*: If the generic lambda has no *trailing-return-type* or the *trailing-return-type* contains a placeholder type, return type deduction of the corresponding function call operator template specialization has to be done. The corresponding specialization is that instantiation of the function call operator template with the same template arguments as those deduced for the conversion function template. Consider the following:

```cpp
auto glambda = [](auto a) { return a; };
int (*fp)(int) = glambda;
```

The behavior of the conversion function of `glambda` above is like that of the following conversion function:

```cpp
struct Closure {
  template<class T> auto operator()(T t) const { /* ... */ }
  template<class T> static auto lambda_call_operator_invoker(T a) {
    // forwards execution to operator()(a) and therefore has
    // the same return type deduced
    /* ... */
  }
  template<class T> using fptr_t =
     decltype(lambda_call_operator_invoker(declval<T>())) (*)(T);

  template<class T> operator fptr_t<T>() const
    { return &lambda_call_operator_invoker; }
};
```

— *end note*]

[*Example 5*:

```cpp
void f1(int (*)(int))   { }
void f2(char (*)(int))  { }

void g(int (*)(int))    { }     // #1
void g(char (*)(char))  { }     // #2

void h(int (*)(int))    { }     // #3
void h(char (*)(int))   { }     // #4

auto glambda = [](auto a) { return a; };
f1(glambda);                    // OK
f2(glambda);                    // error: ID is not convertible
g(glambda);                     // error: ambiguous
h(glambda);                     // OK: calls #3 since it is convertible from ID
int& (*fpi)(int*) = [](auto* a) -> auto& { return *a; };        // OK
```

— *end example*]

**10** The value returned by any given specialization of this conversion function template is the address of a function `F` that, when invoked, has the same effect as invoking the generic lambda’s corresponding function call operator template specialization on a default-constructed instance of the closure type. `F` is a constexpr function if the corresponding specialization is a constexpr function and `F` is an immediate function if the function call operator template specialization is an immediate function. [*Note 6*: This will result in the implicit instantiation of the generic lambda’s body. The instantiated generic lambda’s return type and parameter types are required to match the return type and parameter types of the pointer to function. — *end note*] [*Example 6*:

```cpp
auto GL = [](auto a) { std::cout << a; return a; };
int (*GL_int)(int) = GL;        // OK: through conversion function template
GL_int(3);                      // OK: same as GL(3)
```

— *end example*]

**11** The conversion function or conversion function template is public, constexpr, non-virtual, non-explicit, const, and has a non-throwing exception specification ([[except.spec]](except.spec.md)). [*Example 7*:

```cpp
auto Fwd = [](int (*fp)(int), auto a) { return fp(a); };
auto C = [](auto a) { return a; };

static_assert(Fwd(C,3) == 3);   // OK

// No specialization of the function call operator template can be constexpr (due to the local static).
auto NC = [](auto a) { static int s; return a; };
static_assert(Fwd(NC,3) == 3);  // error
```

— *end example*]

**12** The *lambda-expression*’s *compound-statement* yields the *function-body* ([[dcl.fct.def]](dcl.fct.def.md)) of the function call operator, but for purposes of name lookup ([[basic.lookup]](basic.lookup.md)), determining the type and value of `this` ([[class.this]](class.this.md)) and transforming *id-expression*s referring to non-static class members into class member access expressions using `(*this)` ([[class.mfct.non-static]](class.mfct.non-static.md)), the *compound-statement* is considered in the context of the *lambda-expression*. [*Example 8*:

```cpp
struct S1 {
  int x, y;
  int operator()(int);
  void f() {
    [=]()->int {
      return operator()(this->x + y);   // equivalent to S1::operator()(this->x + (*this).y)
                                        // this has type S1*
    };
  }
};
```

— *end example*] Further, a variable `__func__` is implicitly defined at the beginning of the *compound-statement* of the *lambda-expression*, with semantics as described in [[dcl.fct.def.general]](dcl.fct.def.general.md).

**13** The closure type associated with a *lambda-expression* has no default constructor if the *lambda-expression* has a *lambda-capture* and a defaulted default constructor otherwise. It has a defaulted copy constructor and a defaulted move constructor ([[class.copy.ctor]](class.copy.ctor.md)). It has a deleted copy assignment operator if the *lambda-expression* has a *lambda-capture* and defaulted copy and move assignment operators otherwise ([[class.copy.assign]](class.copy.assign.md)). [*Note 7*: These special member functions are implicitly defined as usual, and might therefore be defined as deleted. — *end note*]

**14** The closure type associated with a *lambda-expression* has an implicitly-declared destructor ([[class.dtor]](class.dtor.md)).

**15** A member of a closure type shall not be explicitly instantiated ([[temp.explicit]](temp.explicit.md)), explicitly specialized ([[temp.expl.spec]](temp.expl.spec.md)), or named in a friend declaration ([[class.friend]](class.friend.md)).
