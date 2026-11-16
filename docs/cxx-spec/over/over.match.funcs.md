# 12.4.1 Candidate functions and argument lists [over.match.funcs]

[Contents](../README.md) · up: [12.4 Overload resolution \[over.match\]](over.match.md) · prev: [12.4 Overload resolution \[over.match\]](over.match.md) · next: [12.4.1.1 Function call syntax \[over.match.call\]](over.match.call.md)

**1** The subclauses of [[over.match.funcs]](over.match.funcs.md) describe the set of candidate functions and the argument list submitted to overload resolution in each context in which overload resolution is used. The source transformations and constructions defined in these subclauses are only for the purpose of describing the overload resolution process. An implementation is not required to use such transformations and constructions.

**2** The set of candidate functions can contain both member and non-member functions to be resolved against the same argument list. So that argument and parameter lists are comparable within this heterogeneous set, a member function is considered to have an extra first parameter, called the *implicit object parameter*, which represents the object for which the member function has been called. For the purposes of overload resolution, both static and non-static member functions have an implicit object parameter, but constructors do not.

**3** Similarly, when appropriate, the context can construct an argument list that contains an *implied object argument* as the first argument in the list to denote the object to be operated on.

**4** For non-static member functions, the type of the implicit object parameter is

- “lvalue reference to *cv* `X`” for functions declared without a *ref-qualifier* or with the `&` *ref-qualifier*
- “rvalue reference to *cv* `X`” for functions declared with the `&&` *ref-qualifier*

where `X` is the class of which the function is a member and *cv* is the cv-qualification on the member function declaration. [*Example 1*: For a `const` member function of class `X`, the extra parameter is assumed to have type “reference to `const X`”. — *end example*] For conversion functions, the function is considered to be a member of the class of the implied object argument for the purpose of defining the type of the implicit object parameter. For non-conversion functions introduced by a *using-declaration* into a derived class, the function is considered to be a member of the derived class for the purpose of defining the type of the implicit object parameter. For static member functions, the implicit object parameter is considered to match any object (since if the function is selected, the object is discarded). [*Note 1*: No actual type is established for the implicit object parameter of a static member function, and no attempt will be made to determine a conversion sequence for that parameter ([[over.match.best]](over.match.best.md)). — *end note*]

**5** During overload resolution, the implied object argument is indistinguishable from other arguments. The implicit object parameter, however, retains its identity since no user-defined conversions can be applied to achieve a type match with it. For non-static member functions declared without a *ref-qualifier*, even if the implicit object parameter is not const-qualified, an rvalue can be bound to the parameter as long as in all other respects the argument can be converted to the type of the implicit object parameter. [*Note 2*: The fact that such an argument is an rvalue does not affect the ranking of implicit conversion sequences ([[over.ics.rank]](over.ics.rank.md)). — *end note*]

**6** Because other than in list-initialization only one user-defined conversion is allowed in an implicit conversion sequence, special rules apply when selecting the best user-defined conversion ([[over.match.best]](over.match.best.md), [[over.best.ics]](over.best.ics.md)). [*Example 2*:

```cpp
class T {
public:
  T();
};

class C : T {
public:
  C(int);
};
T a = 1;            // error: no viable conversion (T(C(1)) not considered)
```

— *end example*]

**7** In each case where a candidate is a function template, candidate function template specializations are generated using template argument deduction ([[temp.over]](../temp/temp.over.md), [[temp.deduct]](../temp/temp.deduct.md)). If a constructor template or conversion function template has an *explicit-specifier* whose *constant-expression* is value-dependent ([[temp.dep]](../temp/temp.dep.md)), template argument deduction is performed first and then, if the context requires a candidate that is not explicit and the generated specialization is explicit ([[dcl.fct.spec]](../dcl/dcl.fct.spec.md)), it will be removed from the candidate set. Those candidates are then handled as candidate functions in the usual way.[^1] A given name can refer to one or more function templates and also to a set of non-template functions. In such a case, the candidate functions generated from each function template are combined with the set of non-template candidate functions.

**8** A defaulted move special member function ([[class.copy.ctor]](../class/class.copy.ctor.md), [[class.copy.assign]](../class/class.copy.assign.md)) that is defined as deleted is excluded from the set of candidate functions in all contexts. A constructor inherited from class type `C` ([[class.inhctor.init]](../class/class.inhctor.init.md)) that has a first parameter of type “reference to *cv1* `P`” (including such a constructor instantiated from a template) is excluded from the set of candidate functions when constructing an object of type *cv2* `D` if the argument list has exactly one argument and `C` is reference-related to `P` and `P` is reference-related to `D`. [*Example 3*:

```cpp
struct A {
  A();                                  // #1
  A(A &&);                              // #2
  template<typename T> A(T &&);         // #3
};
struct B : A {
  using A::A;
  B(const B &);                         // #4
  B(B &&) = default;                    // #5, implicitly deleted

  struct X { X(X &&) = delete; } x;
};
extern B b1;
B b2 = static_cast<B&&>(b1);            // calls #4: #1 is not viable, #2, #3, and #5 are not candidates
struct C { operator B&&(); };
B b3 = C();                             // calls #4
```

— *end example*]

## Subclauses

- [12.4.1.1 Function call syntax \[over.match.call\]](over.match.call.md)
  - [12.4.1.1.1 Call to named function \[over.call.func\]](over.call.func.md)
  - [12.4.1.1.2 Call to object of class type \[over.call.object\]](over.call.object.md)
- [12.4.1.2 Operators in expressions \[over.match.oper\]](over.match.oper.md)
- [12.4.1.3 Initialization by constructor \[over.match.ctor\]](over.match.ctor.md)
- [12.4.1.4 Copy-initialization of class by user-defined conversion \[over.match.copy\]](over.match.copy.md)
- [12.4.1.5 Initialization by conversion function \[over.match.conv\]](over.match.conv.md)
- [12.4.1.6 Initialization by conversion function for direct reference binding \[over.match.ref\]](over.match.ref.md)
- [12.4.1.7 Initialization by list-initialization \[over.match.list\]](over.match.list.md)
- [12.4.1.8 Class template argument deduction \[over.match.class.deduct\]](over.match.class.deduct.md)

[^1]: The process of argument deduction fully determines the parameter types of the function template specializations, i.e., the parameters of function template specializations contain no template parameter types. Therefore, except where specified otherwise, function template specializations and non-template functions ([[dcl.fct]](../dcl/dcl.fct.md)) are treated equivalently for the remainder of overload resolution.
