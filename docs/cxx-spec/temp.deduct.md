# 13.10.2 Template argument deduction [temp.deduct]

[Contents](README.md) · up: [13.10 Function template specializations \[temp.fct.spec\]](temp.fct.spec.md) · prev: [13.10.1 Explicit template argument specification \[temp.arg.explicit\]](temp.arg.explicit.md) · next: [13.10.2.1 Deducing template arguments from a function call \[temp.deduct.call\]](temp.deduct.call.md)

**1** When a function template specialization is referenced, all of the template arguments shall have values. The values can be explicitly specified or, in some cases, be deduced from the use or obtained from default *template-argument*s. [*Example 1*:

```cpp
void f(Array<dcomplex>& cv, Array<int>& ci) {
  sort(cv);                     // calls sort(Array<dcomplex>&)
  sort(ci);                     // calls sort(Array<int>&)
}
```

and

```cpp
void g(double d) {
  int i = convert<int>(d);      // calls convert<int,double>(double)
  int c = convert<char>(d);     // calls convert<char,double>(double)
}
```

— *end example*]

**2** When an explicit template argument list is specified, if the given *template-id* is not valid ([[temp.names]](temp.names.md)), type deduction fails. Otherwise, the specified template argument values are substituted for the corresponding template parameters as specified below.

**3** After this substitution is performed, the function parameter type adjustments described in [[dcl.fct]](dcl.fct.md) are performed. [*Example 2*: A parameter type of “`void (const int, int[5])`” becomes “`void(*)(int,int*)`”. — *end example*] [*Note 1*: A top-level qualifier in a function parameter declaration does not affect the function type but still affects the type of the function parameter variable within the function. — *end note*] [*Example 3*:

```cpp
template <class T> void f(T t);
template <class X> void g(const X x);
template <class Z> void h(Z, Z*);

int main() {
  // #1: function type is f(int), t is non const
  f<int>(1);

  // #2: function type is f(int), t is const
  f<const int>(1);

  // #3: function type is g(int), x is const
  g<int>(1);

  // #4: function type is g(int), x is const
  g<const int>(1);

  // #5: function type is h(int, const int*)
  h<const int>(1,0);
}
```

— *end example*]

**4** [*Note 2*: `f<int>(1)` and `f<const int>(1)` call distinct functions even though both of the functions called have the same function type. — *end note*]

**5** The resulting substituted and adjusted function type is used as the type of the function template for template argument deduction. If a template argument has not been deduced and its corresponding template parameter has a default argument, the template argument is determined by substituting the template arguments determined for preceding template parameters into the default argument. If the substitution results in an invalid type, as described above, type deduction fails. [*Example 4*:

```cpp
template <class T, class U = double>
void f(T t = 0, U u = 0);

void g() {
  f(1, 'c');        // f<int,char>(1,'c')
  f(1);             // f<int,double>(1,0)
  f();              // error: T cannot be deduced
  f<int>();         // f<int,double>(0,0)
  f<int,char>();    // f<int,char>(0,0)
}
```

— *end example*]

When all template arguments have been deduced or obtained from default template arguments, all uses of template parameters in the template parameter list of the template and the function type are replaced with the corresponding deduced or default argument values. If the substitution results in an invalid type, as described above, type deduction fails. If the function template has associated constraints ([[temp.constr.decl]](temp.constr.decl.md)), those constraints are checked for satisfaction ([[temp.constr.constr]](temp.constr.constr.md)). If the constraints are not satisfied, type deduction fails.

**6** At certain points in the template argument deduction process it is necessary to take a function type that makes use of template parameters and replace those template parameters with the corresponding template arguments. This is done at the beginning of template argument deduction when any explicitly specified template arguments are substituted into the function type, and again at the end of template argument deduction when any template arguments that were deduced or obtained from default arguments are substituted.

**7** The substitution occurs in all types and expressions that are used in the function type and in template parameter declarations. The expressions include not only constant expressions such as those that appear in array bounds or as nontype template arguments but also general expressions (i.e., non-constant expressions) inside `sizeof`, `decltype`, and other contexts that allow non-constant expressions. The substitution proceeds in lexical order and stops when a condition that causes deduction to fail is encountered. If substitution into different declarations of the same function template would cause template instantiations to occur in a different order or not at all, the program is ill-formed; no diagnostic required. [*Note 3*: The equivalent substitution in exception specifications is done only when the *noexcept-specifier* is instantiated, at which point a program is ill-formed if the substitution results in an invalid type or expression. — *end note*] [*Example 5*:

```cpp
template <class T> struct A { using X = typename T::X; };
template <class T> typename T::X f(typename A<T>::X);
template <class T> void f(...) { }
template <class T> auto g(typename A<T>::X) -> typename T::X;
template <class T> void g(...) { }
template <class T> typename T::X h(typename A<T>::X);
template <class T> auto h(typename A<T>::X) -> typename T::X;   // redeclaration
template <class T> void h(...) { }

void x() {
  f<int>(0);        // OK, substituting return type causes deduction to fail
  g<int>(0);        // error, substituting parameter type instantiates A<int>
  h<int>(0);        // ill-formed, no diagnostic required
}
```

— *end example*]

**8** If a substitution results in an invalid type or expression, type deduction fails. An invalid type or expression is one that would be ill-formed, with a diagnostic required, if written using the substituted arguments. [*Note 4*: If no diagnostic is required, the program is still ill-formed. Access checking is done as part of the substitution process. — *end note*] Only invalid types and expressions in the immediate context of the function type, its template parameter types, and its *explicit-specifier* can result in a deduction failure. [*Note 5*: The substitution into types and expressions can result in effects such as the instantiation of class template specializations and/or function template specializations, the generation of implicitly-defined functions, etc. Such effects are not in the “immediate context” and can result in the program being ill-formed. — *end note*]

**9** A *lambda-expression* appearing in a function type or a template parameter is not considered part of the immediate context for the purposes of template argument deduction. [*Note 6*: The intent is to avoid requiring implementations to deal with substitution failure involving arbitrary statements. [*Example 6*:

```cpp
template <class T>
  auto f(T) -> decltype([]() { T::invalid; } ());
void f(...);
f(0);               // error: invalid expression not part of the immediate context

template <class T, std::size_t = sizeof([]() { T::invalid; })>
  void g(T);
void g(...);
g(0);               // error: invalid expression not part of the immediate context

template <class T>
  auto h(T) -> decltype([x = T::invalid]() { });
void h(...);
h(0);               // error: invalid expression not part of the immediate context

template <class T>
  auto i(T) -> decltype([]() -> typename T::invalid { });
void i(...);
i(0);               // error: invalid expression not part of the immediate context

template <class T>
  auto j(T t) -> decltype([](auto x) -> decltype(x.invalid) { } (t));   // #1
void j(...);                                                            // #2
j(0);               // deduction fails on #1, calls #2
```

— *end example*] — *end note*]

**10** [*Example 7*:

```cpp
struct X { };
struct Y {
  Y(X){}
};

template <class T> auto f(T t1, T t2) -> decltype(t1 + t2);     // #1
X f(Y, Y);                                                      // #2

X x1, x2;
X x3 = f(x1, x2);   // deduction fails on #1 (cannot add X+X), calls #2
```

— *end example*]

**11** [*Note 7*: Type deduction may fail for the following reasons:

- Attempting to instantiate a pack expansion containing multiple packs of differing lengths.

- Attempting to create an array with an element type that is `void`, a function type, or a reference type, or attempting to create an array with a size that is zero or negative. [*Example 8*:

  ```cpp
  template <class T> int f(T[5]);
  int I = f<int>(0);
  int j = f<void>(0);             // invalid array
  ```

  — *end example*]

- Attempting to use a type that is not a class or enumeration type in a qualified name. [*Example 9*:

  ```cpp
  template <class T> int f(typename T::B*);
  int i = f<int>(0);
  ```

  — *end example*]

- Attempting to use a type in a *nested-name-specifier* of a *qualified-id* when that type does not contain the specified member, or

  - the specified member is not a type where a type is required, or
  - the specified member is not a template where a template is required, or
  - the specified member is not a non-type where a non-type is required.

  [*Example 10*:

  ```cpp
  template <int I> struct X { };
  template <template <class T> class> struct Z { };
  template <class T> void f(typename T::Y*){}
  template <class T> void g(X<T::N>*){}
  template <class T> void h(Z<T::template TT>*){}
  struct A {};
  struct B { int Y; };
  struct C {
    typedef int N;
  };
  struct D {
    typedef int TT;
  };

  int main() {
    // Deduction fails in each of these cases:
    f<A>(0);          // A does not contain a member Y
    f<B>(0);          // The Y member of B is not a type
    g<C>(0);          // The N member of C is not a non-type
    h<D>(0);          // The TT member of D is not a template
  }
  ```

  — *end example*]

- Attempting to create a pointer to reference type.

- Attempting to create a reference to `void`.

- Attempting to create “pointer to member of `T`” when `T` is not a class type. [*Example 11*:

  ```cpp
  template <class T> int f(int T::*);
  int i = f<int>(0);
  ```

  — *end example*]

- Attempting to give an invalid type to a non-type template parameter. [*Example 12*:

  ```cpp
  template <class T, T> struct S {};
  template <class T> int f(S<T, T()>*);
  struct X {};
  int i0 = f<X>(0);
  ```

  — *end example*]

- Attempting to perform an invalid conversion in either a template argument expression, or an expression used in the function declaration. [*Example 13*:

  ```cpp
  template <class T, T*> int f(int);
  int i2 = f<int,1>(0);           // can't conv 1 to int*
  ```

  — *end example*]

- Attempting to create a function type in which a parameter has a type of `void`, or in which the return type is a function type or array type.

— *end note*]

**12** [*Example 14*: In the following example, assuming a `signed char` cannot represent the value 1000, a narrowing conversion ([[dcl.init.list]](dcl.init.list.md)) would be required to convert the *template-argument* of type `int` to `signed char`, therefore substitution fails for the second template ([[temp.arg.nontype]](temp.arg.nontype.md)).

```cpp
template <int> int f(int);
template <signed char> int f(int);
int i1 = f<1000>(0);            // OK
int i2 = f<1>(0);               // ambiguous; not narrowing
```

— *end example*]

## Subclauses

- [13.10.2.1 Deducing template arguments from a function call \[temp.deduct.call\]](temp.deduct.call.md)
- [13.10.2.2 Deducing template arguments taking the address of a function template \[temp.deduct.funcaddr\]](temp.deduct.funcaddr.md)
- [13.10.2.3 Deducing conversion function template arguments \[temp.deduct.conv\]](temp.deduct.conv.md)
- [13.10.2.4 Deducing template arguments during partial ordering \[temp.deduct.partial\]](temp.deduct.partial.md)
- [13.10.2.5 Deducing template arguments from a type \[temp.deduct.type\]](temp.deduct.type.md)
- [13.10.2.6 Deducing template arguments from a function declaration \[temp.deduct.decl\]](temp.deduct.decl.md)
