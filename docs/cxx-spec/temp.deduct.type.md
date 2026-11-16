# 13.10.2.5 Deducing template arguments from a type [temp.deduct.type]

[Contents](README.md) · up: [13.10.2 Template argument deduction \[temp.deduct\]](temp.deduct.md) · prev: [13.10.2.4 Deducing template arguments during partial ordering \[temp.deduct.partial\]](temp.deduct.partial.md) · next: [13.10.2.6 Deducing template arguments from a function declaration \[temp.deduct.decl\]](temp.deduct.decl.md)

**1** Template arguments can be deduced in several different contexts, but in each case a type that is specified in terms of template parameters (call it `P`) is compared with an actual type (call it `A`), and an attempt is made to find template argument values (a type for a type parameter, a value for a non-type parameter, or a template for a template parameter) that will make `P`, after substitution of the deduced values (call it the deduced `A`), compatible with `A`.

**2** In some cases, the deduction is done using a single set of types `P` and `A`, in other cases, there will be a set of corresponding types `P` and `A`. Type deduction is done independently for each `P/A` pair, and the deduced template argument values are then combined. If type deduction cannot be done for any `P/A` pair, or if for any pair the deduction leads to more than one possible set of deduced values, or if different pairs yield different deduced values, or if any template argument remains neither deduced nor explicitly specified, template argument deduction fails. The type of a type parameter is only deduced from an array bound if it is not otherwise deduced.

**3** A given type `P` can be composed from a number of other types, templates, and non-type values:

- A function type includes the types of each of the function parameters and the return type.
- A pointer-to-member type includes the type of the class object pointed to and the type of the member pointed to.
- A type that is a specialization of a class template (e.g., `A<int>`) includes the types, templates, and non-type values referenced by the template argument list of the specialization.
- An array type includes the array element type and the value of the array bound.

**4** In most cases, the types, templates, and non-type values that are used to compose `P` participate in template argument deduction. That is, they may be used to determine the value of a template argument, and template argument deduction fails if the value so determined is not consistent with the values determined elsewhere. In certain contexts, however, the value does not participate in type deduction, but instead uses the values of template arguments that were either deduced elsewhere or explicitly specified. If a template parameter is used only in non-deduced contexts and is not explicitly specified, template argument deduction fails. [*Note 1*: Under [[temp.deduct.call]](temp.deduct.call.md), if `P` contains no *template-parameter*s that appear in deduced contexts, no deduction is done, so `P` and `A` need not have the same form. — *end note*]

**5** The non-deduced contexts are:

- The *nested-name-specifier* of a type that was specified using a *qualified-id*.

- The *expression* of a *decltype-specifier*.

- A non-type template argument or an array bound in which a subexpression references a template parameter.

- A template parameter used in the parameter type of a function parameter that has a default argument that is being used in the call for which argument deduction is being done.

- A function parameter for which the associated argument is an overload set ([[over.over]](over.over.md)), and one or more of the following apply:

  - more than one function matches the function parameter type (resulting in an ambiguous deduction), or
  - no function matches the function parameter type, or
  - the overload set supplied as an argument contains one or more function templates.

- A function parameter for which the associated argument is an initializer list ([[dcl.init.list]](dcl.init.list.md)) but the parameter does not have a type for which deduction from an initializer list is specified ([[temp.deduct.call]](temp.deduct.call.md)). [*Example 1*:

  ```cpp
  template<class T> void g(T);
  g({1,2,3});                 // error: no argument deduced for T
  ```

  — *end example*]

- A function parameter pack that does not occur at the end of the *parameter-declaration-list*.

**6** When a type name is specified in a way that includes a non-deduced context, all of the types that comprise that type name are also non-deduced. However, a compound type can include both deduced and non-deduced types. [*Example 2*: If a type is specified as `A<T>::B<T2>`, both `T` and `T2` are non-deduced. Likewise, if a type is specified as `A<I+J>::X<T>`, `I`, `J`, and `T` are non-deduced. If a type is specified as `void` `f(typename` `A<T>::B,` `A<T>)`, the `T` in `A<T>::B` is non-deduced but the `T` in `A<T>` is deduced. — *end example*]

**7** [*Example 3*: Here is an example in which different parameter/argument pairs produce inconsistent template argument deductions:

```cpp
template<class T> void f(T x, T y) { /* ... */ }
struct A { /* ... */ };
struct B : A { /* ... */ };
void g(A a, B b) {
  f(a,b);           // error: T could be A or B
  f(b,a);           // error: T could be A or B
  f(a,a);           // OK: T is A
  f(b,b);           // OK: T is B
}
```

Here is an example where two template arguments are deduced from a single function parameter/argument pair. This can lead to conflicts that cause type deduction to fail:

```cpp
template <class T, class U> void f(  T (*)( T, U, U )  );

int g1( int, float, float);
char g2( int, float, float);
int g3( int, char, float);

void r() {
  f(g1);            // OK: T is int and U is float
  f(g2);            // error: T could be char or int
  f(g3);            // error: U could be char or float
}
```

Here is an example where a qualification conversion applies between the argument type on the function call and the deduced template argument type:

```cpp
template<class T> void f(const T*) { }
int* p;
void s() {
  f(p);             // f(const int*)
}
```

Here is an example where the template argument is used to instantiate a derived class type of the corresponding function parameter type:

```cpp
template <class T> struct B { };
template <class T> struct D : public B<T> {};
struct D2 : public B<int> {};
template <class T> void f(B<T>&){}
void t() {
  D<int> d;
  D2     d2;
  f(d);             // calls f(B<int>&)
  f(d2);            // calls f(B<int>&)
}
```

— *end example*]

**8** A template type argument `T`, a template template argument `TT` or a template non-type argument `i` can be deduced if `P` and `A` have one of the following forms:

```cpp
T
cv T
T*
T&
T&&
T[integer-constant]
template-name<T>  (where template-name refers to a class template)
type(T)
T()
T(T)
T type::*
type T::*
T T::*
T (type::*)()
type (T::*)()
type (type::*)(T)
type (T::*)(T)
T (type::*)(T)
T (T::*)()
T (T::*)(T)
type[i]
template-name<i>  (where template-name refers to a class template)
TT<T>
TT<i>
TT<>
```

where `(T)` represents a parameter-type-list ([[dcl.fct]](dcl.fct.md)) where at least one parameter type contains a `T`, and `()` represents a parameter-type-list where no parameter type contains a `T`. Similarly, `<T>` represents template argument lists where at least one argument contains a `T`, `<i>` represents template argument lists where at least one argument contains an `i` and `<>` represents template argument lists where no argument contains a `T` or an `i`.

**9** If `P` has a form that contains `<T>` or `<i>`, then each argument `P`<sub>*i*</sub> of the respective template argument list of `P` is compared with the corresponding argument `A`<sub>*i*</sub> of the corresponding template argument list of `A`. If the template argument list of `P` contains a pack expansion that is not the last template argument, the entire template argument list is a non-deduced context. If `P`<sub>*i*</sub> is a pack expansion, then the pattern of `P`<sub>*i*</sub> is compared with each remaining argument in the template argument list of `A`. Each comparison deduces template arguments for subsequent positions in the template parameter packs expanded by `P`<sub>*i*</sub>. During partial ordering ([[temp.deduct.partial]](temp.deduct.partial.md)), if `A`<sub>*i*</sub> was originally a pack expansion:

- if `P` does not contain a template argument corresponding to `A`<sub>*i*</sub> then `A`<sub>*i*</sub> is ignored;
- otherwise, if `P`<sub>*i*</sub> is not a pack expansion, template argument deduction fails.

[*Example 4*:

```cpp
template<class T1, class... Z> class S;                                 // #1
template<class T1, class... Z> class S<T1, const Z&...> { };            // #2
template<class T1, class T2>   class S<T1, const T2&> { };              // #3
S<int, const int&> s;           // both #2 and #3 match; #3 is more specialized

template<class T, class... U>            struct A { };                  // #1
template<class T1, class T2, class... U> struct A<T1, T2*, U...> { };   // #2
template<class T1, class T2>             struct A<T1, T2> { };          // #3
template struct A<int, int*>;   // selects #2
```

— *end example*]

**10** Similarly, if `P` has a form that contains `(T)`, then each parameter type `P`<sub>*i*</sub> of the respective parameter-type-list ([[dcl.fct]](dcl.fct.md)) of `P` is compared with the corresponding parameter type `A`<sub>*i*</sub> of the corresponding parameter-type-list of `A`. If `P` and `A` are function types that originated from deduction when taking the address of a function template ([[temp.deduct.funcaddr]](temp.deduct.funcaddr.md)) or when deducing template arguments from a function declaration ([[temp.deduct.decl]](temp.deduct.decl.md)) and `P`<sub>*i*</sub> and `A`<sub>*i*</sub> are parameters of the top-level parameter-type-list of `P` and `A`, respectively, `P`<sub>*i*</sub> is adjusted if it is a forwarding reference ([[temp.deduct.call]](temp.deduct.call.md)) and `A`<sub>*i*</sub> is an lvalue reference, in which case the type of `P`<sub>*i*</sub> is changed to be the template parameter type (i.e., `T&&` is changed to simply `T`). [*Note 2*: As a result, when `P`<sub>*i*</sub> is `T&&` and `A`<sub>*i*</sub> is `X&`, the adjusted `P`<sub>*i*</sub> will be `T`, causing `T` to be deduced as `X&`. — *end note*] [*Example 5*:

```cpp
template <class T> void f(T&&);
template <> void f(int&) { }    // #1
template <> void f(int&&) { }   // #2
void g(int i) {
  f(i);                         // calls f<int&>(int&), i.e., #1
  f(0);                         // calls f<int>(int&&), i.e., #2
}
```

— *end example*]

If the *parameter-declaration* corresponding to `P`<sub>*i*</sub> is a function parameter pack, then the type of its *declarator-id* is compared with each remaining parameter type in the parameter-type-list of `A`. Each comparison deduces template arguments for subsequent positions in the template parameter packs expanded by the function parameter pack. During partial ordering ([[temp.deduct.partial]](temp.deduct.partial.md)), if `A`<sub>*i*</sub> was originally a function parameter pack:

- if `P` does not contain a function parameter type corresponding to `A`<sub>*i*</sub> then `A`<sub>*i*</sub> is ignored;
- otherwise, if `P`<sub>*i*</sub> is not a function parameter pack, template argument deduction fails.

[*Example 6*:

```cpp
template<class T, class... U> void f(T*, U...) { }  // #1
template<class T>             void f(T) { }         // #2
template void f(int*);                              // selects #1
```

— *end example*]

**11** These forms can be used in the same way as `T` is for further composition of types. [*Example 7*:

```cpp
X<int> (*)(char[6])
```

is of the form

```cpp
template-name<T> (*)(type[i])
```

which is a variant of

```cpp
type (*)(T)
```

where type is `X<int>` and `T` is `char[6]`. — *end example*]

**12** Template arguments cannot be deduced from function arguments involving constructs other than the ones specified above.

**13** When the value of the argument corresponding to a non-type template parameter `P` that is declared with a dependent type is deduced from an expression, the template parameters in the type of `P` are deduced from the type of the value. [*Example 8*:

```cpp
template<long n> struct A { };

template<typename T> struct C;
template<typename T, T n> struct C<A<n>> {
  using Q = T;
};

using R = long;
using R = C<A<2>>::Q;           // OK; T was deduced as long from the
                                // template argument value in the type A<2>
```

— *end example*]

**14** The type of `N` in the type `T[N]` is `std::size_t`. [*Example 9*:

```cpp
template<typename T> struct S;
template<typename T, T n> struct S<int[n]> {
  using Q = T;
};

using V = decltype(sizeof 0);
using V = S<int[42]>::Q;        // OK; T was deduced as std::size_t from the type int[42]
```

— *end example*]

**15** [*Example 10*:

```cpp
template<class T, T i> void f(int (&a)[i]);
int v[10];
void g() {
  f(v);                         // OK: T is std::size_t
}
```

— *end example*]

**16** [*Note 3*: Except for reference and pointer types, a major array bound is not part of a function parameter type and cannot be deduced from an argument:

```cpp
template<int i> void f1(int a[10][i]);
template<int i> void f2(int a[i][20]);
template<int i> void f3(int (&a)[i][20]);

void g() {
  int v[10][20];
  f1(v);                        // OK: i deduced as 20
  f1<20>(v);                    // OK
  f2(v);                        // error: cannot deduce template-argument i
  f2<10>(v);                    // OK
  f3(v);                        // OK: i deduced as 10
}
```

— *end note*]

**17** [*Note 4*: If, in the declaration of a function template with a non-type template parameter, the non-type template parameter is used in a subexpression in the function parameter list, the expression is a non-deduced context as specified above. [*Example 11*:

```cpp
template <int i> class A { /* ... */ };
template <int i> void g(A<i+1>);
template <int i> void f(A<i>, A<i+1>);
void k() {
  A<1> a1;
  A<2> a2;
  g(a1);                        // error: deduction fails for expression i+1
  g<0>(a1);                     // OK
  f(a1, a2);                    // OK
}
```

— *end example*] — *end note*]

**18** [*Note 5*: Template parameters do not participate in template argument deduction if they are used only in non-deduced contexts. For example,

```cpp
template<int i, typename T>
T deduce(typename A<T>::X x,    // T is not deduced here
         T t,                   // but T is deduced here
         typename B<i>::Y y);   // i is not deduced here
A<int> a;
B<77>  b;

int    x = deduce<77>(a.xm, 62, b.ym);
// T deduced as int; a.xm must be convertible to A<int>::X
// i is explicitly specified to be 77; b.ym must be convertible to B<77>::Y
```

— *end note*]

**19** If `P` has a form that contains `<i>`, and if the type of `i` differs from the type of the corresponding template parameter of the template named by the enclosing *simple-template-id*, deduction fails. If `P` has a form that contains `[i]`, and if the type of `i` is not an integral type, deduction fails.[^1] [*Example 12*:

```cpp
template<int i> class A { /* ... */ };
template<short s> void f(A<s>);
void k1() {
  A<1> a;
  f(a);             // error: deduction fails for conversion from int to short
  f<1>(a);          // OK
}

template<const short cs> class B { };
template<short s> void g(B<s>);
void k2() {
  B<1> b;
  g(b);             // OK: cv-qualifiers are ignored on template parameter types
}
```

— *end example*]

**20** A *template-argument* can be deduced from a function, pointer to function, or pointer-to-member-function type.

[*Example 13*:

```cpp
template<class T> void f(void(*)(T,int));
template<class T> void foo(T,int);
void g(int,int);
void g(char,int);

void h(int,int,int);
void h(char,int);
int m() {
  f(&g);            // error: ambiguous
  f(&h);            // OK: void h(char,int) is a unique match
  f(&foo);          // error: type deduction fails because foo is a template
}
```

— *end example*]

**21** A template *type-parameter* cannot be deduced from the type of a function default argument. [*Example 14*:

```cpp
template <class T> void f(T = 5, T = 7);
void g() {
  f(1);             // OK: call f<int>(1,7)
  f();              // error: cannot deduce T
  f<int>();         // OK: call f<int>(5,7)
}
```

— *end example*]

**22** The *template-argument* corresponding to a template *template-parameter* is deduced from the type of the *template-argument* of a class template specialization used in the argument list of a function call. [*Example 15*:

```cpp
template <template <class T> class X> struct A { };
template <template <class T> class X> void f(A<X>) { }
template<class T> struct B { };
A<B> ab;
f(ab);              // calls f(A<B>)
```

— *end example*]

**23** [*Note 6*: Template argument deduction involving parameter packs ([[temp.variadic]](temp.variadic.md)) can deduce zero or more arguments for each parameter pack. — *end note*] [*Example 16*:

```cpp
template<class> struct X { };
template<class R, class ... ArgTypes> struct X<R(int, ArgTypes ...)> { };
template<class ... Types> struct Y { };
template<class T, class ... Types> struct Y<T, Types& ...> { };

template<class ... Types> int f(void (*)(Types ...));
void g(int, float);

X<int> x1;                      // uses primary template
X<int(int, float, double)> x2;  // uses partial specialization; ArgTypes contains float, double
X<int(float, int)> x3;          // uses primary template
Y<> y1;                         // use primary template; Types is empty
Y<int&, float&, double&> y2;    // uses partial specialization; T is int&, Types contains float, double
Y<int, float, double> y3;       // uses primary template; Types contains int, float, double
int fv = f(g);                  // OK; Types contains int, float
```

— *end example*]

[^1]: Although the *template-argument* corresponding to a *template-parameter* of type `bool` may be deduced from an array bound, the resulting value will always be `true` because the array bound will be nonzero.
