# 13.7.6.2 Partial ordering of function templates [temp.func.order]

[Contents](README.md) · up: [13.7.6 Function templates \[temp.fct\]](temp.fct.md) · prev: [13.7.6.1 Function template overloading \[temp.over.link\]](temp.over.link.md) · next: [13.7.7 Alias templates \[temp.alias\]](temp.alias.md)

**1** If a function template is overloaded, the use of a function template specialization might be ambiguous because template argument deduction ([[temp.deduct]](temp.deduct.md)) may associate the function template specialization with more than one function template declaration. *Partial ordering* of overloaded function template declarations is used in the following contexts to select the function template to which a function template specialization refers:

- during overload resolution for a call to a function template specialization ([[over.match.best]](over.match.best.md));
- when the address of a function template specialization is taken;
- when a placement operator delete that is a function template specialization is selected to match a placement operator new ([[basic.stc.dynamic.deallocation]](basic.stc.dynamic.deallocation.md), [[expr.new]](expr.new.md));
- when a friend function declaration ([[temp.friend]](temp.friend.md)), an explicit instantiation ([[temp.explicit]](temp.explicit.md)) or an explicit specialization ([[temp.expl.spec]](temp.expl.spec.md)) refers to a function template specialization.

**2** Partial ordering selects which of two function templates is more specialized than the other by transforming each template in turn (see next paragraph) and performing template argument deduction using the function type. The deduction process determines whether one of the templates is more specialized than the other. If so, the more specialized template is the one chosen by the partial ordering process. If both deductions succeed, the partial ordering selects the more constrained template (if one exists) as determined below.

**3** To produce the transformed template, for each type, non-type, or template template parameter (including template parameter packs ([[temp.variadic]](temp.variadic.md)) thereof) synthesize a unique type, value, or class template respectively and substitute it for each occurrence of that parameter in the function type of the template. [*Note 1*: The type replacing the placeholder in the type of the value synthesized for a non-type template parameter is also a unique synthesized type. — *end note*] Each function template *M* that is a member function is considered to have a new first parameter of type *X*(*M*), described below, inserted in its function parameter list. If exactly one of the function templates was considered by overload resolution via a rewritten candidate ([[over.match.oper]](over.match.oper.md)) with a reversed order of parameters, then the order of the function parameters in its transformed template is reversed. For a function template *M* with cv-qualifiers *cv* that is a member of a class *A*:

- The type *X*(*M*) is “rvalue reference to *cv* *A*” if the optional *ref-qualifier* of *M* is `&&` or if *M* has no *ref-qualifier* and the positionally-corresponding parameter of the other transformed template has rvalue reference type; if this determination depends recursively upon whether *X*(*M*) is an rvalue reference type, it is not considered to have rvalue reference type.
- Otherwise, *X*(*M*) is “lvalue reference to *cv* *A*”.

[*Note 2*: This allows a non-static member to be ordered with respect to a non-member function and for the results to be equivalent to the ordering of two equivalent non-members. — *end note*] [*Example 1*:

```cpp
struct A { };
template<class T> struct B {
  template<class R> int operator*(R&);              // #1
};

template<class T, class R> int operator*(T&, R&);   // #2

// The declaration of B::operator* is transformed into the equivalent of
// template<class R> int operator*(B<A>&, R&); // #1a

int main() {
  A a;
  B<A> b;
  b * a;                                            // calls #1
}
```

— *end example*]

**4** Using the transformed function template’s function type, perform type deduction against the other template as described in [[temp.deduct.partial]](temp.deduct.partial.md).

[*Example 2*:

```cpp
template<class T> struct A { A(); };

template<class T> void f(T);
template<class T> void f(T*);
template<class T> void f(const T*);

template<class T> void g(T);
template<class T> void g(T&);

template<class T> void h(const T&);
template<class T> void h(A<T>&);

void m() {
  const int* p;
  f(p);             // f(const T*) is more specialized than f(T) or f(T*)
  float x;
  g(x);             // ambiguous: g(T) or g(T&)
  A<int> z;
  h(z);             // overload resolution selects h(A<T>&)
  const A<int> z2;
  h(z2);            // h(const T&) is called because h(A<T>&) is not callable
}
```

— *end example*]

**5** [*Note 3*: Since, in a call context, such type deduction considers only parameters for which there are explicit call arguments, some parameters are ignored (namely, function parameter packs, parameters with default arguments, and ellipsis parameters). [*Example 3*:

```cpp
template<class T> void f(T);                            // #1
template<class T> void f(T*, int=1);                    // #2
template<class T> void g(T);                            // #3
template<class T> void g(T*, ...);                      // #4
```

```cpp
int main() {
  int* ip;
  f(ip);                                                // calls #2
  g(ip);                                                // calls #4
}
```

— *end example*] [*Example 4*:

```cpp
template<class T, class U> struct A { };

template<class T, class U> void f(U, A<U, T>* p = 0);   // #1
template<         class U> void f(U, A<U, U>* p = 0);   // #2
template<class T         > void g(T, T = T());          // #3
template<class T, class... U> void g(T, U ...);         // #4

void h() {
  f<int>(42, (A<int, int>*)0);                          // calls #2
  f<int>(42);                                           // error: ambiguous
  g(42);                                                // error: ambiguous
}
```

— *end example*] [*Example 5*:

```cpp
template<class T, class... U> void f(T, U...);          // #1
template<class T            > void f(T);                // #2
template<class T, class... U> void g(T*, U...);         // #3
template<class T            > void g(T);                // #4

void h(int i) {
  f(&i);                                                // OK: calls #2
  g(&i);                                                // OK: calls #3
}
```

— *end example*] — *end note*]

**6** If deduction against the other template succeeds for both transformed templates, constraints can be considered as follows:

- If their *template-parameter-list*s (possibly including *template-parameter*s invented for an abbreviated function template ([[dcl.fct]](dcl.fct.md))) or function parameter lists differ in length, neither template is more specialized than the other.

- Otherwise:

  - If exactly one of the templates was considered by overload resolution via a rewritten candidate with reversed order of parameters:

    - If, for either template, some of the template parameters are not deducible from their function parameters, neither template is more specialized than the other.

    - If there is either no reordering or more than one reordering of the associated *template-parameter-list* such that

      - the corresponding *template-parameter*s of the *template-parameter-list*s are equivalent and
      - the function parameters that positionally correspond between the two templates are of the same type,

      neither template is more specialized than the other.

  - Otherwise, if the corresponding *template-parameter*s of the *template-parameter-list*s are not equivalent ([[temp.over.link]](temp.over.link.md)) or if the function parameters that positionally correspond between the two templates are not of the same type, neither template is more specialized than the other.

- Otherwise, if the context in which the partial ordering is done is that of a call to a conversion function and the return types of the templates are not the same, then neither template is more specialized than the other.

- Otherwise, if one template is more constrained than the other ([[temp.constr.order]](temp.constr.order.md)), the more constrained template is more specialized than the other.

- Otherwise, neither template is more specialized than the other.

[*Example 6*:

```cpp
template <typename> constexpr bool True = true;
template <typename T> concept C = True<T>;

void f(C auto &, auto &) = delete;
template <C Q> void f(Q &, C auto &);

void g(struct A *ap, struct B *bp) {
  f(*ap, *bp);                  // OK: Can use different methods to produce template parameters
}

template <typename T, typename U> struct X {};

template <typename T, C U, typename V> bool operator==(X<T, U>, V) = delete;
template <C T, C U, C V>               bool operator==(T, X<U, V>);

void h() {
  X<void *, int>{} == 0;        // OK: Correspondence of [T, U, V] and [U, V, T]
}
```

— *end example*]
