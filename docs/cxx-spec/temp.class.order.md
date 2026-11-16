# 13.7.5.2 Partial ordering of class template specializations [temp.class.order]

[Contents](README.md) · up: [13.7.5 Class template partial specializations \[temp.class.spec\]](temp.class.spec.md) · prev: [13.7.5.1 Matching of class template partial specializations \[temp.class.spec.match\]](temp.class.spec.match.md) · next: [13.7.5.3 Members of class template specializations \[temp.class.spec.mfunc\]](temp.class.spec.mfunc.md)

**1** For two class template partial specializations, the first is *more specialized* than the second if, given the following rewrite to two function templates, the first function template is more specialized than the second according to the ordering rules for function templates ([[temp.func.order]](temp.func.order.md)):

- Each of the two function templates has the same template parameters and associated constraints ([[temp.constr.decl]](temp.constr.decl.md)) as the corresponding partial specialization.
- Each function template has a single function parameter whose type is a class template specialization where the template arguments are the corresponding template parameters from the function template for each template argument in the *template-argument-list* of the *simple-template-id* of the partial specialization.

**2** [*Example 1*:

```cpp
template<int I, int J, class T> class X { };
template<int I, int J>          class X<I, J, int> { };         // #1
template<int I>                 class X<I, I, int> { };         // #2

template<int I0, int J0> void f(X<I0, J0, int>);                // A
template<int I0>         void f(X<I0, I0, int>);                // B

template <auto v>    class Y { };
template <auto* p>   class Y<p> { };                            // #3
template <auto** pp> class Y<pp> { };                           // #4

template <auto* p0>   void g(Y<p0>);                            // C
template <auto** pp0> void g(Y<pp0>);                           // D
```

According to the ordering rules for function templates, the function template *B* is more specialized than the function template *A* and the function template *D* is more specialized than the function template *C*. Therefore, the partial specialization #2 is more specialized than the partial specialization #1 and the partial specialization #4 is more specialized than the partial specialization #3. — *end example*] [*Example 2*:

```cpp
template<typename T> concept C = requires (T t) { t.f(); };
template<typename T> concept D = C<T> && requires (T t) { t.f(); };

template<typename T> class S { };
template<C T> class S<T> { };   // #1
template<D T> class S<T> { };   // #2

template<C T> void f(S<T>);     // A
template<D T> void f(S<T>);     // B
```

The partial specialization #2 is more specialized than #1 because `B` is more specialized than `A`. — *end example*]
