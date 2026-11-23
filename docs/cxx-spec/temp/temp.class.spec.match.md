# 13.7.5.1 Matching of class template partial specializations [temp.class.spec.match]

[Contents](../README.md) · up: [13.7.5 Class template partial specializations \[temp.class.spec\]](temp.class.spec.md) · prev: [13.7.5 Class template partial specializations \[temp.class.spec\]](temp.class.spec.md) · next: [13.7.5.2 Partial ordering of class template specializations \[temp.class.order\]](temp.class.order.md)

**1** When a class template is used in a context that requires an instantiation of the class, it is necessary to determine whether the instantiation is to be generated using the primary template or one of the partial specializations. This is done by matching the template arguments of the class template specialization with the template argument lists of the partial specializations.

- If exactly one matching specialization is found, the instantiation is generated from that specialization.
- If more than one matching specialization is found, the partial order rules ([[temp.class.order]](temp.class.order.md)) are used to determine whether one of the specializations is more specialized than the others. If none of the specializations is more specialized than all of the other matching specializations, then the use of the class template is ambiguous and the program is ill-formed.
- If no matches are found, the instantiation is generated from the primary template.

**2** A partial specialization matches a given actual template argument list if the template arguments of the partial specialization can be deduced from the actual template argument list ([[temp.deduct]](temp.deduct.md)), and the deduced template arguments satisfy the associated constraints of the partial specialization, if any ([[temp.constr.decl]](temp.constr.decl.md)). [*Example 1*:

```cpp
template<class T1, class T2, int I> class A             { };    // #1
template<class T, int I>            class A<T, T*, I>   { };    // #2
template<class T1, class T2, int I> class A<T1*, T2, I> { };    // #3
template<class T>                   class A<int, T*, 5> { };    // #4
template<class T1, class T2, int I> class A<T1, T2*, I> { };    // #5

A<int, int, 1>   a1;                    // uses #1
A<int, int*, 1>  a2;                    // uses #2, T is int, I is 1
A<int, char*, 5> a3;                    // uses #4, T is char
A<int, char*, 1> a4;                    // uses #5, T1 is int, T2 is char, I is 1
A<int*, int*, 2> a5;                    // ambiguous: matches #3 and #5
```

— *end example*] [*Example 2*:

```cpp
template<typename T> concept C = requires (T t) { t.f(); };

template<typename T> struct S { };      // #1
template<C T> struct S<T> { };          // #2

struct Arg { void f(); };

S<int> s1;                              // uses #1; the constraints of #2 are not satisfied
S<Arg> s2;                              // uses #2; both constraints are satisfied but #2 is more specialized
```

— *end example*]

**3** If the template arguments of a partial specialization cannot be deduced because of the structure of its *template-parameter-list* and the *template-id*, the program is ill-formed. [*Example 3*:

```cpp
template <int I, int J> struct A {};
template <int I> struct A<I+5, I*2> {};     // error

template <int I> struct A<I, I> {};         // OK

template <int I, int J, int K> struct B {};
template <int I> struct B<I, I*2, 2> {};    // OK
```

— *end example*]

**4** In a type name that refers to a class template specialization, (e.g., `A<int, int, 1>`) the argument list shall match the template parameter list of the primary template. The template arguments of a specialization are deduced from the arguments of the primary template.
