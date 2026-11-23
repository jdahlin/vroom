# 13.10.1 Explicit template argument specification [temp.arg.explicit]

[Contents](../README.md) · up: [13.10 Function template specializations \[temp.fct.spec\]](temp.fct.spec.md) · prev: [13.10 Function template specializations \[temp.fct.spec\]](temp.fct.spec.md) · next: [13.10.2 Template argument deduction \[temp.deduct\]](temp.deduct.md)

**1** Template arguments can be specified when referring to a function template specialization that is not a specialization of a constructor template by qualifying the function template name with the list of *template-argument*s in the same way as *template-argument*s are specified in uses of a class template specialization. [*Example 1*:

```cpp
template<class T> void sort(Array<T>& v);
void f(Array<dcomplex>& cv, Array<int>& ci) {
  sort<dcomplex>(cv);                   // sort(Array<dcomplex>&)
  sort<int>(ci);                        // sort(Array<int>&)
}
```

and

```cpp
template<class U, class V> U convert(V v);

void g(double d) {
  int i = convert<int,double>(d);       // int convert(double)
  char c = convert<char,double>(d);     // char convert(double)
}
```

— *end example*]

**2** Template arguments shall not be specified when referring to a specialization of a constructor template ([[class.ctor]](../class/class.ctor.md), [[class.qual]](../basic/class.qual.md)).

**3** A template argument list may be specified when referring to a specialization of a function template

- when a function is called,
- when the address of a function is taken, when a function initializes a reference to function, or when a pointer to member function is formed,
- in an explicit specialization,
- in an explicit instantiation, or
- in a friend declaration.

**4** Trailing template arguments that can be deduced ([[temp.deduct]](temp.deduct.md)) or obtained from default *template-argument*s may be omitted from the list of explicit *template-argument*s. A trailing template parameter pack ([[temp.variadic]](temp.variadic.md)) not otherwise deduced will be deduced as an empty sequence of template arguments. If all of the template arguments can be deduced, they may all be omitted; in this case, the empty template argument list `<>` itself may also be omitted. In contexts where deduction is done and fails, or in contexts where deduction is not done, if a template argument list is specified and it, along with any default template arguments, identifies a single function template specialization, then the *template-id* is an lvalue for the function template specialization. [*Example 2*:

```cpp
template<class X, class Y> X f(Y);
template<class X, class Y, class ... Z> X g(Y);
void h() {
  int i = f<int>(5.6);          // Y deduced as double
  int j = f(5.6);               // error: X cannot be deduced
  f<void>(f<int, bool>);        // Y for outer f deduced as int (*)(bool)
  f<void>(f<int>);              // error: f<int> does not denote a single function template specialization
  int k = g<int>(5.6);          // Y deduced as double; Z deduced as an empty sequence
  f<void>(g<int, bool>);        // Y for outer f deduced as int (*)(bool),
                                // Z deduced as an empty sequence
}
```

— *end example*]

**5** [*Note 1*: An empty template argument list can be used to indicate that a given use refers to a specialization of a function template even when a non-template function ([[dcl.fct]](../dcl/dcl.fct.md)) is visible that would otherwise be used. For example:

```cpp
template <class T> int f(T);    // #1
int f(int);                     // #2
int k = f(1);                   // uses #2
int l = f<>(1);                 // uses #1
```

— *end note*]

**6** Template arguments that are present shall be specified in the declaration order of their corresponding *template-parameter*s. The template argument list shall not specify more *template-argument*s than there are corresponding *template-parameter*s unless one of the *template-parameter*s is a template parameter pack. [*Example 3*:

```cpp
template<class X, class Y, class Z> X f(Y,Z);
template<class ... Args> void f2();
void g() {
  f<int,const char*,double>("aa",3.0);
  f<int,const char*>("aa",3.0); // Z deduced as double
  f<int>("aa",3.0);             // Y deduced as const char*; Z deduced as double
  f("aa",3.0);                  // error: X cannot be deduced
  f2<char, short, int, long>(); // OK
}
```

— *end example*]

**7** Implicit conversions ([[conv]](../expr/conv.md)) will be performed on a function argument to convert it to the type of the corresponding function parameter if the parameter type contains no *template-parameter*s that participate in template argument deduction. [*Note 2*: Template parameters do not participate in template argument deduction if they are explicitly specified. For example,

```cpp
template<class T> void f(T);

class Complex {
  Complex(double);
};

void g() {
  f<Complex>(1);    // OK, means f<Complex>(Complex(1))
}
```

— *end note*]

**8** [*Note 3*: Because the explicit template argument list follows the function template name, and because constructor templates ([[class.ctor]](../class/class.ctor.md)) are named without using a function name ([[class.qual]](../basic/class.qual.md)), there is no way to provide an explicit template argument list for these function templates. — *end note*]

**9** Template argument deduction can extend the sequence of template arguments corresponding to a template parameter pack, even when the sequence contains explicitly specified template arguments. [*Example 4*:

```cpp
template<class ... Types> void f(Types ... values);

void g() {
  f<int*, float*>(0, 0, 0);     // Types deduced as the sequence int*, float*, int
}
```

— *end example*]
