# 13.7.5 Class template partial specializations [temp.class.spec]

[Contents](README.md) · up: [13.7 Template declarations \[temp.decls\]](temp.decls.md) · prev: [13.7.4 Friends \[temp.friend\]](temp.friend.md) · next: [13.7.5.1 Matching of class template partial specializations \[temp.class.spec.match\]](temp.class.spec.match.md)

**1** A *primary class template* declaration is one in which the class template name is an identifier. A template declaration in which the class template name is a *simple-template-id* is a *partial specialization* of the class template named in the *simple-template-id*. A partial specialization of a class template provides an alternative definition of the template that is used instead of the primary definition when the arguments in a specialization match those given in the partial specialization ([[temp.class.spec.match]](temp.class.spec.match.md)). The primary template shall be declared before any specializations of that template. A partial specialization shall be declared before the first use of a class template specialization that would make use of the partial specialization as the result of an implicit or explicit instantiation in every translation unit in which such a use occurs; no diagnostic is required.

**2** Each class template partial specialization is a distinct template and definitions shall be provided for the members of a template partial specialization ([[temp.class.spec.mfunc]](temp.class.spec.mfunc.md)).

**3** [*Example 1*:

```cpp
template<class T1, class T2, int I> class A             { };
template<class T, int I>            class A<T, T*, I>   { };
template<class T1, class T2, int I> class A<T1*, T2, I> { };
template<class T>                   class A<int, T*, 5> { };
template<class T1, class T2, int I> class A<T1, T2*, I> { };
```

The first declaration declares the primary (unspecialized) class template. The second and subsequent declarations declare partial specializations of the primary template. — *end example*]

**4** A class template partial specialization may be constrained ([[temp.pre]](temp.pre.md)). [*Example 2*:

```cpp
template<typename T> concept C = true;

template<typename T> struct X { };
template<typename T> struct X<T*> { };          // #1
template<C T> struct X<T> { };                  // #2
```

Both partial specializations are more specialized than the primary template. #1 is more specialized because the deduction of its template arguments from the template argument list of the class template specialization succeeds, while the reverse does not. #2 is more specialized because the template arguments are equivalent, but the partial specialization is more constrained ([[temp.constr.order]](temp.constr.order.md)). — *end example*]

**5** The template parameters are specified in the angle bracket enclosed list that immediately follows the keyword `template`. For partial specializations, the template argument list is explicitly written immediately following the class template name. For primary templates, this list is implicitly described by the template parameter list. Specifically, the order of the template arguments is the sequence in which they appear in the template parameter list. [*Example 3*: The template argument list for the primary template in the example above is `<T1,` `T2,` `I>`. — *end example*] [*Note 1*: The template argument list cannot be specified in the primary template declaration. For example,

```cpp
template<class T1, class T2, int I>
class A<T1, T2, I> { };                         // error
```

— *end note*]

**6** A class template partial specialization may be declared in any scope in which the corresponding primary template may be defined ([[namespace.memdef]](namespace.memdef.md), [[class.mem]](class.mem.md), [[temp.mem]](temp.mem.md)). [*Example 4*:

```cpp
template<class T> struct A {
  struct C {
    template<class T2> struct B { };
    template<class T2> struct B<T2**> { };      // partial specialization #1
  };
};

// partial specialization of A<T>::C::B<T2>
template<class T> template<class T2>
  struct A<T>::C::B<T2*> { };                   // #2

A<short>::C::B<int*> absip;                     // uses partial specialization #2
```

— *end example*]

**7** Partial specialization declarations themselves are not found by name lookup. Rather, when the primary template name is used, any previously-declared partial specializations of the primary template are also considered. One consequence is that a *using-declaration* which refers to a class template does not restrict the set of partial specializations which may be found through the *using-declaration*. [*Example 5*:

```cpp
namespace N {
  template<class T1, class T2> class A { };     // primary template
}

using N::A;                                     // refers to the primary template

namespace N {
  template<class T> class A<T, T*> { };         // partial specialization
}

A<int,int*> a;      // uses the partial specialization, which is found through the using-declaration
                    // which refers to the primary template
```

— *end example*]

**8** A non-type argument is non-specialized if it is the name of a non-type parameter. All other non-type arguments are specialized.

**9** Within the argument list of a class template partial specialization, the following restrictions apply:

- The type of a template parameter corresponding to a specialized non-type argument shall not be dependent on a parameter of the specialization. [*Example 6*:

  ```cpp
  template <class T, T t> struct C {};
  template <class T> struct C<T, 1>;              // error

  template< int X, int (*array_ptr)[X] > class A {};
  int array[5];
  template< int X > class A<X,&array> { };        // error
  ```

  — *end example*]

- The specialization shall be more specialized than the primary template ([[temp.class.order]](temp.class.order.md)).

- The template parameter list of a specialization shall not contain default template argument values.[^1]

- An argument shall not contain an unexpanded pack. If an argument is a pack expansion ([[temp.variadic]](temp.variadic.md)), it shall be the last argument in the template argument list.

**10** The usual access checking rules do not apply to non-dependent names used to specify template arguments of the *simple-template-id* of the partial specialization. [*Note 2*: The template arguments may be private types or objects that would normally not be accessible. Dependent names cannot be checked when declaring the partial specialization, but will be checked when substituting into the partial specialization. — *end note*]

## Subclauses

- [13.7.5.1 Matching of class template partial specializations \[temp.class.spec.match\]](temp.class.spec.match.md)
- [13.7.5.2 Partial ordering of class template specializations \[temp.class.order\]](temp.class.order.md)
- [13.7.5.3 Members of class template specializations \[temp.class.spec.mfunc\]](temp.class.spec.mfunc.md)

[^1]: There is no way in which they could be used.
