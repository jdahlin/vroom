# 13.10.2.1 Deducing template arguments from a function call [temp.deduct.call]

[Contents](README.md) · up: [13.10.2 Template argument deduction \[temp.deduct\]](temp.deduct.md) · prev: [13.10.2 Template argument deduction \[temp.deduct\]](temp.deduct.md) · next: [13.10.2.2 Deducing template arguments taking the address of a function template \[temp.deduct.funcaddr\]](temp.deduct.funcaddr.md)

**1** Template argument deduction is done by comparing each function template parameter type (call it `P`) that contains *template-parameter*s that participate in template argument deduction with the type of the corresponding argument of the call (call it `A`) as described below. If removing references and cv-qualifiers from `P` gives `std::initializer_list<P^′>` or `P`'`[N]` for some `P`' and `N` and the argument is a non-empty initializer list ([[dcl.init.list]](dcl.init.list.md)), then deduction is performed instead for each element of the initializer list independently, taking `P`' as separate function template parameter types `P`'<sub>*i*</sub> and the *i*<sup></sup>*th* initializer element as the corresponding argument. In the `P`'`[N]` case, if `N` is a non-type template parameter, `N` is deduced from the length of the initializer list. Otherwise, an initializer list argument causes the parameter to be considered a non-deduced context ([[temp.deduct.type]](temp.deduct.type.md)). [*Example 1*:

```cpp
template<class T> void f(std::initializer_list<T>);
f({1,2,3});                     // T deduced as int
f({1,"asdf"});                  // error: T deduced as both int and const char*

template<class T> void g(T);
g({1,2,3});                     // error: no argument deduced for T

template<class T, int N> void h(T const(&)[N]);
h({1,2,3});                     // T deduced as int; N deduced as 3

template<class T> void j(T const(&)[3]);
j({42});                        // T deduced as int; array bound not considered

struct Aggr { int i; int j; };
template<int N> void k(Aggr const(&)[N]);
k({1,2,3});                     // error: deduction fails, no conversion from int to Aggr
k({{1},{2},{3}});               // OK, N deduced as 3

template<int M, int N> void m(int const(&)[M][N]);
m({{1,2},{3,4}});               // M and N both deduced as 2

template<class T, int N> void n(T const(&)[N], T);
n({{1},{2},{3}},Aggr());        // OK, T is Aggr, N is 3

template<typename T, int N> void o(T (* const (&)[N])(T)) { }
int f1(int);
int f4(int);
char f4(char);
o({ &f1, &f4 });                                // OK, T deduced as int from first element, nothing
                                                // deduced from second element, N deduced as 2
o({ &f1, static_cast<char(*)(char)>(&f4) });    // error: conflicting deductions for T
```

— *end example*] For a function parameter pack that occurs at the end of the *parameter-declaration-list*, deduction is performed for each remaining argument of the call, taking the type `P` of the *declarator-id* of the function parameter pack as the corresponding function template parameter type. Each deduction deduces template arguments for subsequent positions in the template parameter packs expanded by the function parameter pack. When a function parameter pack appears in a non-deduced context ([[temp.deduct.type]](temp.deduct.type.md)), the type of that pack is never deduced. [*Example 2*:

```cpp
template<class ... Types> void f(Types& ...);
template<class T1, class ... Types> void g(T1, Types ...);
template<class T1, class ... Types> void g1(Types ..., T1);

void h(int x, float& y) {
  const int z = x;
  f(x, y, z);                   // Types deduced as int, float, const int
  g(x, y, z);                   // T1 deduced as int; Types deduced as float, int
  g1(x, y, z);                  // error: Types is not deduced
  g1<int, int, int>(x, y, z);   // OK, no deduction occurs
}
```

— *end example*]

**2** If `P` is not a reference type:

- If `A` is an array type, the pointer type produced by the array-to-pointer standard conversion ([[conv.array]](conv.array.md)) is used in place of `A` for type deduction; otherwise,
- If `A` is a function type, the pointer type produced by the function-to-pointer standard conversion ([[conv.func]](conv.func.md)) is used in place of `A` for type deduction; otherwise,
- If `A` is a cv-qualified type, the top-level cv-qualifiers of `A`’s type are ignored for type deduction.

**3** If `P` is a cv-qualified type, the top-level cv-qualifiers of `P`’s type are ignored for type deduction. If `P` is a reference type, the type referred to by `P` is used for type deduction. [*Example 3*:

```cpp
template<class T> int f(const T&);
int n1 = f(5);                  // calls f<int>(const int&)
const int i = 0;
int n2 = f(i);                  // calls f<int>(const int&)
template <class T> int g(volatile T&);
int n3 = g(i);                  // calls g<const int>(const volatile int&)
```

— *end example*] A *forwarding reference* is an rvalue reference to a cv-unqualified template parameter that does not represent a template parameter of a class template (during class template argument deduction ([[over.match.class.deduct]](over.match.class.deduct.md))). If `P` is a forwarding reference and the argument is an lvalue, the type “lvalue reference to `A`” is used in place of `A` for type deduction. [*Example 4*:

```cpp
template <class T> int f(T&& heisenreference);
template <class T> int g(const T&&);
int i;
int n1 = f(i);                  // calls f<int&>(int&)
int n2 = f(0);                  // calls f<int>(int&&)
int n3 = g(i);                  // error: would call g<int>(const int&&), which
                                // would bind an rvalue reference to an lvalue

template <class T> struct A {
  template <class U>
    A(T&&, U&&, int*);          // #1: T&& is not a forwarding reference.
                                // U&& is a forwarding reference.
  A(T&&, int*);                 // #2
};

template <class T> A(T&&, int*) -> A<T>;    // #3: T&& is a forwarding reference.

int *ip;
A a{i, 0, ip};                  // error: cannot deduce from #1
A a0{0, 0, ip};                 // uses #1 to deduce A<int> and #1 to initialize
A a2{i, ip};                    // uses #3 to deduce A<int&> and #2 to initialize
```

— *end example*]

**4** In general, the deduction process attempts to find template argument values that will make the deduced `A` identical to `A` (after the type `A` is transformed as described above). However, there are three cases that allow a difference:

- If the original `P` is a reference type, the deduced `A` (i.e., the type referred to by the reference) can be more cv-qualified than the transformed `A`.

- The transformed `A` can be another pointer or pointer-to-member type that can be converted to the deduced `A` via a function pointer conversion ([[conv.fctptr]](conv.fctptr.md)) and/or qualification conversion ([[conv.qual]](conv.qual.md)).

- If `P` is a class and `P` has the form *simple-template-id*, then the transformed `A` can be a derived class `D` of the deduced `A`. Likewise, if `P` is a pointer to a class of the form *simple-template-id*, the transformed `A` can be a pointer to a derived class `D` pointed to by the deduced `A`. However, if there is a class `C` that is a (direct or indirect) base class of `D` and derived (directly or indirectly) from a class `B` and that would be a valid deduced `A`, the deduced `A` cannot be `B` or pointer to `B`, respectively. [*Example 5*:

  ```cpp
  template <typename... T> struct X;
  template <> struct X<> {};
  template <typename T, typename... Ts>
    struct X<T, Ts...> : X<Ts...> {};
  struct D : X<int> {};

  template <typename... T>
  int f(const X<T...>&);
  int x = f(D());     // calls f<int>, not f<>
                      // B is X<>, C is X<int>
  ```

  — *end example*]

**5** These alternatives are considered only if type deduction would otherwise fail. If they yield more than one possible deduced `A`, the type deduction fails. [*Note 1*: If a *template-parameter* is not used in any of the function parameters of a function template, or is used only in a non-deduced context, its corresponding *template-argument* cannot be deduced from a function call and the *template-argument* must be explicitly specified. — *end note*]

**6** When `P` is a function type, function pointer type, or pointer-to-member-function type:

- If the argument is an overload set containing one or more function templates, the parameter is treated as a non-deduced context.
- If the argument is an overload set (not containing function templates), trial argument deduction is attempted using each of the members of the set. If deduction succeeds for only one of the overload set members, that member is used as the argument value for the deduction. If deduction succeeds for more than one member of the overload set the parameter is treated as a non-deduced context.

**7** [*Example 6*:

```cpp
// Only one function of an overload set matches the call so the function parameter is a deduced context.
template <class T> int f(T (*p)(T));
int g(int);
int g(char);
int i = f(g);       // calls f(int (*)(int))
```

— *end example*]

**8** [*Example 7*:

```cpp
// Ambiguous deduction causes the second function parameter to be a non-deduced context.
template <class T> int f(T, T (*p)(T));
int g(int);
char g(char);
int i = f(1, g);    // calls f(int, int (*)(int))
```

— *end example*]

**9** [*Example 8*:

```cpp
// The overload set contains a template, causing the second function parameter to be a non-deduced context.
template <class T> int f(T, T (*p)(T));
char g(char);
template <class T> T g(T);
int i = f(1, g);    // calls f(int, int (*)(int))
```

— *end example*]

**10** If deduction succeeds for all parameters that contain *template-parameter*s that participate in template argument deduction, and all template arguments are explicitly specified, deduced, or obtained from default template arguments, remaining parameters are then compared with the corresponding arguments. For each remaining parameter `P` with a type that was non-dependent before substitution of any explicitly-specified template arguments, if the corresponding argument `A` cannot be implicitly converted to `P`, deduction fails. [*Note 2*: Parameters with dependent types in which no *template-parameter*s participate in template argument deduction, and parameters that became non-dependent due to substitution of explicitly-specified template arguments, will be checked during overload resolution. — *end note*] [*Example 9*:

```cpp
template <class T> struct Z {
  typedef typename T::x xx;
};
template <class T> typename Z<T>::xx f(void *, T);      // #1
template <class T> void f(int, T);                      // #2
struct A {} a;
int main() {
  f(1, a);          // OK, deduction fails for #1 because there is no conversion from int to void*
}
```

— *end example*]
