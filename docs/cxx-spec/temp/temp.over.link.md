# 13.7.6.1 Function template overloading [temp.over.link]

[Contents](../README.md) · up: [13.7.6 Function templates \[temp.fct\]](temp.fct.md) · prev: [13.7.6 Function templates \[temp.fct\]](temp.fct.md) · next: [13.7.6.2 Partial ordering of function templates \[temp.func.order\]](temp.func.order.md)

**1** It is possible to overload function templates so that two different function template specializations have the same type. [*Example 1*:

```cpp
// translation unit 1:
template<class T>
  void f(T*);
void g(int* p) {
  f(p); // calls f<int>(int*)
}
```

```cpp
// translation unit 2:
template<class T>
  void f(T);
void h(int* p) {
  f(p); // calls f<int*>(int*)
}
```

— *end example*]

**2** Such specializations are distinct functions and do not violate the one-definition rule ([[basic.def.odr]](../basic/basic.def.odr.md)).

**3** The signature of a function template is defined in [[intro.defs]](../intro/intro.defs.md). The names of the template parameters are significant only for establishing the relationship between the template parameters and the rest of the signature. [*Note 1*: Two distinct function templates may have identical function return types and function parameter lists, even if overload resolution alone cannot distinguish them.

```cpp
template<class T> void f();
template<int I> void f();       // OK: overloads the first template
                                // distinguishable with an explicit template argument list
```

— *end note*]

**4** When an expression that references a template parameter is used in the function parameter list or the return type in the declaration of a function template, the expression that references the template parameter is part of the signature of the function template. This is necessary to permit a declaration of a function template in one translation unit to be linked with another declaration of the function template in another translation unit and, conversely, to ensure that function templates that are intended to be distinct are not linked with one another. [*Example 2*:

```cpp
template <int I, int J> A<I+J> f(A<I>, A<J>);   // #1
template <int K, int L> A<K+L> f(A<K>, A<L>);   // same as #1
template <int I, int J> A<I-J> f(A<I>, A<J>);   // different from #1
```

— *end example*] [*Note 2*: Most expressions that use template parameters use non-type template parameters, but it is possible for an expression to reference a type parameter. For example, a template type parameter can be used in the `sizeof` operator. — *end note*]

**5** Two expressions involving template parameters are considered *equivalent* if two function definitions containing the expressions would satisfy the one-definition rule ([[basic.def.odr]](../basic/basic.def.odr.md)), except that the tokens used to name the template parameters may differ as long as a token used to name a template parameter in one expression is replaced by another token that names the same template parameter in the other expression. Two unevaluated operands that do not involve template parameters are considered equivalent if two function definitions containing the expressions would satisfy the one-definition rule, except that the tokens used to name types and declarations may differ as long as they name the same entities, and the tokens used to form concept-ids may differ as long as the two *template-id*s are the same ([[temp.type]](temp.type.md)). [*Note 3*: For instance, `A<42>` and `A<40+2>` name the same type. — *end note*] Two *lambda-expression*s are never considered equivalent. [*Note 4*: The intent is to avoid *lambda-expression*s appearing in the signature of a function template with external linkage. — *end note*] For determining whether two dependent names ([[temp.dep]](temp.dep.md)) are equivalent, only the name itself is considered, not the result of name lookup in the context of the template. If multiple declarations of the same function template differ in the result of this name lookup, the result for the first declaration is used. [*Example 3*:

```cpp
template <int I, int J> void f(A<I+J>);         // #1
template <int K, int L> void f(A<K+L>);         // same as #1

template <class T> decltype(g(T())) h();
int g(int);
template <class T> decltype(g(T())) h()         // redeclaration of h() uses the earlier lookup...
  { return g(T()); }                            // ... although the lookup here does find g(int)
int i = h<int>();                               // template argument substitution fails; g(int)
                                                // was not in scope at the first declaration of h()

// ill-formed, no diagnostic required: the two expressions are functionally equivalent but not equivalent
template <int N> void foo(const char (*s)[([]{}, N)]);
template <int N> void foo(const char (*s)[([]{}, N)]);

// two different declarations because the non-dependent portions are not considered equivalent
template <class T> void spam(decltype([]{}) (*s)[sizeof(T)]);
template <class T> void spam(decltype([]{}) (*s)[sizeof(T)]);
```

— *end example*] Two potentially-evaluated expressions involving template parameters that are not equivalent are *functionally equivalent* if, for any given set of template arguments, the evaluation of the expression results in the same value. Two unevaluated operands that are not equivalent are functionally equivalent if, for any given set of template arguments, the expressions perform the same operations in the same order with the same entities. [*Note 5*: For instance, one could have redundant parentheses. — *end note*]

**6** Two *template-head*s are *equivalent* if their *template-parameter-list*s have the same length, corresponding *template-parameter*s are equivalent and are both declared with *type-constraint*s that are equivalent if either *template-parameter* is declared with a *type-constraint*, and if either *template-head* has a *requires-clause*, they both have *requires-clause*s and the corresponding *constraint-expression*s are equivalent. Two *template-parameter*s are *equivalent* under the following conditions:

- they declare template parameters of the same kind,
- if either declares a template parameter pack, they both do,
- if they declare non-type template parameters, they have equivalent types ignoring the use of *type-constraint*s for placeholder types, and
- if they declare template template parameters, their template parameters are equivalent.

When determining whether types or *type-constraint*s are equivalent, the rules above are used to compare expressions involving template parameters. Two *template-head*s are *functionally equivalent* if they accept and are satisfied by ([[temp.constr.constr]](temp.constr.constr.md)) the same set of template argument lists.

**7** Two function templates are *equivalent* if they are declared in the same scope, have the same name, have equivalent *template-head*s, and have return types, parameter lists, and trailing *requires-clause*s (if any) that are equivalent using the rules described above to compare expressions involving template parameters. Two function templates are *functionally equivalent* if they are declared in the same scope, have the same name, accept and are satisfied by the same set of template argument lists, and have return types and parameter lists that are functionally equivalent using the rules described above to compare expressions involving template parameters. If the validity or meaning of the program depends on whether two constructs are equivalent, and they are functionally equivalent but not equivalent, the program is ill-formed, no diagnostic required.

**8** [*Note 6*: This rule guarantees that equivalent declarations will be linked with one another, while not requiring implementations to use heroic efforts to guarantee that functionally equivalent declarations will be treated as distinct. For example, the last two declarations are functionally equivalent and would cause a program to be ill-formed:

```cpp
// guaranteed to be the same
template <int I> void f(A<I>, A<I+10>);
template <int I> void f(A<I>, A<I+10>);

// guaranteed to be different
template <int I> void f(A<I>, A<I+10>);
template <int I> void f(A<I>, A<I+11>);

// ill-formed, no diagnostic required
template <int I> void f(A<I>, A<I+10>);
template <int I> void f(A<I>, A<I+1+2+3+4>);
```

— *end note*]
