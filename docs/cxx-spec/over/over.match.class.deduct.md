# 12.4.1.8 Class template argument deduction [over.match.class.deduct]

[Contents](../README.md) · up: [12.4.1 Candidate functions and argument lists \[over.match.funcs\]](over.match.funcs.md) · prev: [12.4.1.7 Initialization by list-initialization \[over.match.list\]](over.match.list.md) · next: [12.4.2 Viable functions \[over.match.viable\]](over.match.viable.md)

**1** When resolving a placeholder for a deduced class type ([[dcl.type.class.deduct]](../dcl/dcl.type.class.deduct.md)) where the *template-name* names a primary class template `C`, a set of functions and function templates, called the guides of `C`, is formed comprising:

- If `C` is defined, for each constructor of `C`, a function template with the following properties:

  - The template parameters are the template parameters of `C` followed by the template parameters (including default template arguments) of the constructor, if any.
  - The types of the function parameters are those of the constructor.
  - The return type is the class template specialization designated by `C` and template arguments corresponding to the template parameters of `C`.

- If `C` is not defined or does not declare any constructors, an additional function template derived as above from a hypothetical constructor `C()`.

- An additional function template derived as above from a hypothetical constructor `C(C)`, called the *copy deduction candidate*.

- For each *deduction-guide*, a function or function template with the following properties:

  - The template parameters, if any, and function parameters are those of the *deduction-guide*.
  - The return type is the *simple-template-id* of the *deduction-guide*.

In addition, if `C` is defined and its definition satisfies the conditions for an aggregate class ([[dcl.init.aggr]](../dcl/dcl.init.aggr.md)) with the assumption that any dependent base class has no virtual functions and no virtual base classes, and the initializer is a non-empty *braced-init-list* or parenthesized *expression-list*, and there are no *deduction-guide*s for `C`, the set contains an additional function template, called the *aggregate deduction candidate*, defined as follows. Let *x*<sub>1</sub>, …, *x*<sub>*n*</sub> be the elements of the *initializer-list* or *designated-initializer-list* of the *braced-init-list*, or of the *expression-list*. For each *x*<sub>*i*</sub>, let *e*<sub>*i*</sub> be the corresponding aggregate element of `C` or of one of its (possibly recursive) subaggregates that would be initialized by *x*<sub>*i*</sub> ([[dcl.init.aggr]](../dcl/dcl.init.aggr.md)) if

- brace elision is not considered for any aggregate element that has a dependent non-array type or an array type with a value-dependent bound, and
- each non-trailing aggregate element that is a pack expansion is assumed to correspond to no elements of the initializer list, and
- a trailing aggregate element that is a pack expansion is assumed to correspond to all remaining elements of the initializer list (if any).

If there is no such aggregate element *e*<sub>*i*</sub> for any *x*<sub>*i*</sub>, the aggregate deduction candidate is not added to the set. The aggregate deduction candidate is derived as above from a hypothetical constructor `C`(`T`<sub>1</sub>, …, `T`<sub>*n*</sub>), where

- if *e*<sub>*i*</sub> is of array type and *x*<sub>*i*</sub> is a *braced-init-list* or *string-literal*, `T`<sub>*i*</sub> is an rvalue reference to the declared type of *e*<sub>*i*</sub>, and
- otherwise, `T`<sub>*i*</sub> is the declared type of *e*<sub>*i*</sub>,

except that additional parameter packs of the form `P`<sub>*j*</sub> `...` are inserted into the parameter list in their original aggregate element position corresponding to each non-trailing aggregate element of type `P`<sub>*j*</sub> that was skipped because it was a parameter pack, and the trailing sequence of parameters corresponding to a trailing aggregate element that is a pack expansion (if any) is replaced by a single parameter of the form `T`<sub>*n*</sub> `...`.

**2** When resolving a placeholder for a deduced class type ([[dcl.type.simple]](../dcl/dcl.type.simple.md)) where the *template-name* names an alias template `A`, the *defining-type-id* of `A` must be of the form

```bnf
'typename'ₒₚₜ nested-name-specifierₒₚₜ 'template'ₒₚₜ simple-template-id
```

as specified in [[dcl.type.simple]](../dcl/dcl.type.simple.md). The guides of `A` are the set of functions or function templates formed as follows. For each function or function template `f` in the guides of the template named by the *simple-template-id* of the *defining-type-id*, the template arguments of the return type of `f` are deduced from the *defining-type-id* of `A` according to the process in [[temp.deduct.type]](../temp/temp.deduct.type.md) with the exception that deduction does not fail if not all template arguments are deduced. Let `g` denote the result of substituting these deductions into `f`. If substitution succeeds, form a function or function template `f'` with the following properties and add it to the set of guides of `A`:

- The function type of `f'` is the function type of `g`.
- If `f` is a function template, `f'` is a function template whose template parameter list consists of all the template parameters of `A` (including their default template arguments) that appear in the above deductions or (recursively) in their default template arguments, followed by the template parameters of `f` that were not deduced (including their default template arguments), otherwise `f'` is not a function template.
- The associated constraints ([[temp.constr.decl]](../temp/temp.constr.decl.md)) are the conjunction of the associated constraints of `g` and a constraint that is satisfied if and only if the arguments of `A` are deducible (see below) from the return type.
- If `f` is a copy deduction candidate ([[over.match.class.deduct]](over.match.class.deduct.md)), then `f'` is considered to be so as well.
- If `f` was generated from a *deduction-guide* ([[over.match.class.deduct]](over.match.class.deduct.md)), then `f'` is considered to be so as well.
- The *explicit-specifier* of `f'` is the *explicit-specifier* of `g` (if any).

**3** The arguments of a template `A` are said to be deducible from a type `T` if, given a class template

```cpp
template <typename> class AA;
```

with a single partial specialization whose template parameter list is that of `A` and whose template argument list is a specialization of `A` with the template argument list of `A` ([[temp.dep.type]](../temp/temp.dep.type.md)), `AA<T>` matches the partial specialization.

**4** Initialization and overload resolution are performed as described in [[dcl.init]](../dcl/dcl.init.md) and [[over.match.ctor]](over.match.ctor.md), [[over.match.copy]](over.match.copy.md), or [[over.match.list]](over.match.list.md) (as appropriate for the type of initialization performed) for an object of a hypothetical class type, where the guides of the template named by the placeholder are considered to be the constructors of that class type for the purpose of forming an overload set, and the initializer is provided by the context in which class template argument deduction was performed. The following exceptions apply:

- The first phase in [[over.match.list]](over.match.list.md) (considering initializer-list constructors) is omitted if the initializer list consists of a single expression of type *cv* `U`, where `U` is, or is derived from, a specialization of the class template directly or indirectly named by the placeholder.
- During template argument deduction for the aggregate deduction candidate, the number of elements in a trailing parameter pack is only deduced from the number of remaining function arguments if it is not otherwise deduced.

If the function or function template was generated from a constructor or *deduction-guide* that had an *explicit-specifier*, each such notional constructor is considered to have that same *explicit-specifier*. All such notional constructors are considered to be public members of the hypothetical class type.

**5** [*Example 1*:

```cpp
template <class T> struct A {
  explicit A(const T&, ...) noexcept;               // #1
  A(T&&, ...);                                      // #2
};

int i;
A a1 = { i, i };    // error: explicit constructor #1 selected in copy-list-initialization during deduction,
                    // cannot deduce from non-forwarding rvalue reference in #2

A a2{i, i};         // OK, #1 deduces to A<int> and also initializes
A a3{0, i};         // OK, #2 deduces to A<int> and also initializes
A a4 = {0, i};      // OK, #2 deduces to A<int> and also initializes

template <class T> A(const T&, const T&) -> A<T&>;  // #3
template <class T> explicit A(T&&, T&&) -> A<T>;    // #4

A a5 = {0, 1};      // error: explicit deduction guide #4 selected in copy-list-initialization during deduction
A a6{0,1};          // OK, #4 deduces to A<int> and #2 initializes
A a7 = {0, i};      // error: #3 deduces to A<int&>, #1 and #2 declare same constructor
A a8{0,i};          // error: #3 deduces to A<int&>, #1 and #2 declare same constructor

template <class T> struct B {
  template <class U> using TA = T;
  template <class U> B(U, TA<U>);
};

B b{(int*)0, (char*)0};         // OK, deduces B<char*>

template <typename T>
struct S {
  T x;
  T y;
};

template <typename T>
struct C {
  S<T> s;
  T t;
};

template <typename T>
struct D {
  S<int> s;
  T t;
};

C c1 = {1, 2};                  // error: deduction failed
C c2 = {1, 2, 3};               // error: deduction failed
C c3 = {{1u, 2u}, 3};           // OK, deduces C<int>

D d1 = {1, 2};                  // error: deduction failed
D d2 = {1, 2, 3};               // OK, braces elided, deduces D<int>

template <typename T>
struct E {
  T t;
  decltype(t) t2;
};

E e1 = {1, 2};                  // OK, deduces E<int>

template <typename... T>
struct Types {};

template <typename... T>
struct F : Types<T...>, T... {};

struct X {};
struct Y {};
struct Z {};
struct W { operator Y(); };

F f1 = {Types<X, Y, Z>{}, {}, {}};      // OK, F<X, Y, Z> deduced
F f2 = {Types<X, Y, Z>{}, X{}, Y{}};    // OK, F<X, Y, Z> deduced
F f3 = {Types<X, Y, Z>{}, X{}, W{}};    // error: conflicting types deduced; operator Y not considered
```

— *end example*]

**6** [*Example 2*:

```cpp
template <class T, class U> struct C {
  C(T, U);                                      // #1
};
template<class T, class U>
  C(T, U) -> C<T, std::type_identity_t<U>>;     // #2

template<class V> using A = C<V *, V *>;
template<std::integral W> using B = A<W>;

int i{};
double d{};
A a1(&i, &i);   // deduces A<int>
A a2(i, i);     // error: cannot deduce V * from i
A a3(&i, &d);   // error: #1: cannot deduce (V*, V*) from (int *, double *)
                // #2: cannot deduce A<V> from C<int *, double *>
B b1(&i, &i);   // deduces B<int>
B b2(&d, &d);   // error: cannot deduce B<W> from C<double *, double *>
```

Possible exposition-only implementation of the above procedure:

```cpp
// The following concept ensures a specialization of A is deduced.
template <class> class AA;
template <class V> class AA<A<V>> { };
template <class T> concept deduces_A = requires { sizeof(AA<T>); };

// f1 is formed from the constructor #1 of C, generating the following function template
template<T, U>
  auto f1(T, U) -> C<T, U>;

// Deducing arguments for C<T, U> from C<V *, V*> deduces T as V * and U as V *;
// f1' is obtained by transforming f1 as described by the above procedure.
template<class V> requires deduces_A<C<V *, V *>>
  auto f1_prime(V *, V*) -> C<V *, V *>;

// f2 is formed from the deduction-guide #2 of C
template<class T, class U> auto f2(T, U) -> C<T, std::type_identity_t<U>>;

// Deducing arguments for C<T, std::type_identity_t<U>> from C<V *, V*> deduces T as V *;
// f2' is obtained by transforming f2 as described by the above procedure.
template<class V, class U>
  requires deduces_A<C<V *, std::type_identity_t<U>>>
  auto f2_prime(V *, U) -> C<V *, std::type_identity_t<U>>;

// The following concept ensures a specialization of B is deduced.
template <class> class BB;
template <class V> class BB<B<V>> { };
template <class T> concept deduces_B = requires { sizeof(BB<T>); };

// The guides for B derived from the above f1' and f2' for A are as follows:
template<std::integral W>
  requires deduces_A<C<W *, W *>> && deduces_B<C<W *, W *>>
  auto f1_prime_for_B(W *, W *) -> C<W *, W *>;

template<std::integral W, class U>
  requires deduces_A<C<W *, std::type_identity_t<U>>> &&
    deduces_B<C<W *, std::type_identity_t<U>>>
  auto f2_prime_for_B(W *, U) -> C<W *, std::type_identity_t<U>>;
```

— *end example*]
