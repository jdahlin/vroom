# 13.2 Template parameters [temp.param]

[Contents](README.md) · up: [13 Templates \[temp\]](temp.md) · prev: [13.1 Preamble \[temp.pre\]](temp.pre.md) · next: [13.3 Names of template specializations \[temp.names\]](temp.names.md)

**1** The syntax for *template-parameter*s is:

```bnf
template-parameter:
  type-parameter
  parameter-declaration
```

```bnf
type-parameter:
  type-parameter-key '...'ₒₚₜ identifierₒₚₜ
  type-parameter-key identifierₒₚₜ '=' type-id
  type-constraint '...'ₒₚₜ identifierₒₚₜ
  type-constraint identifierₒₚₜ '=' type-id
  template-head type-parameter-key '...'ₒₚₜ identifierₒₚₜ
  template-head type-parameter-key identifierₒₚₜ '=' id-expression
```

```bnf
type-parameter-key:
  'class'
  'typename'
```

```bnf
type-constraint:
  nested-name-specifierₒₚₜ concept-name
  nested-name-specifierₒₚₜ concept-name '<' template-argument-listₒₚₜ '>'
```

[*Note 1*: The `>` token following the *template-parameter-list* of a *type-parameter* may be the product of replacing a `>>` token by two consecutive `>` tokens ([[temp.names]](temp.names.md)). — *end note*]

**2** There is no semantic difference between `class` and `typename` in a *type-parameter-key*. `typename` followed by an *unqualified-id* names a template type parameter. `typename` followed by a *qualified-id* denotes the type in a non-type[^1] *parameter-declaration*. A *template-parameter* of the form `class` *identifier* is a *type-parameter*. [*Example 1*:

```cpp
class T { /* ... */ };
int i;

template<class T, T i> void f(T t) {
  T t1 = i;         // template-parameters T and i
  ::T t2 = ::i;     // global namespace members T and i
}
```

Here, the template `f` has a *type-parameter* called `T`, rather than an unnamed non-type *template-parameter* of class `T`. — *end example*] A storage class shall not be specified in a *template-parameter* declaration. Types shall not be defined in a *template-parameter* declaration.

**3** A *type-parameter* whose identifier does not follow an ellipsis defines its *identifier* to be a *typedef-name* (if declared without `template`) or *template-name* (if declared with `template`) in the scope of the template declaration. [*Note 2*: A template argument may be a class template or alias template. For example,

```cpp
template<class T> class myarray { /* ... */ };

template<class K, class V, template<class T> class C = myarray>
class Map {
  C<K> key;
  C<V> value;
};
```

— *end note*]

**4** A *type-constraint* `Q` that designates a concept `C` can be used to constrain a contextually-determined type or template type parameter pack `T` with a *constraint-expression* `E` defined as follows. If `Q` is of the form `C<A_1, ..., A_n>`, then let `E'` be `C<T, A_1, ..., A_n>`. Otherwise, let `E'` be `C<T>`. If `T` is not a pack, then `E` is `E'`, otherwise `E` is `(E' && ...)`. This *constraint-expression* `E` is called the *immediately-declared constraint* of `Q` for `T`. The concept designated by a *type-constraint* shall be a type concept ([[temp.concept]](temp.concept.md)).

**5** A *type-parameter* that starts with a *type-constraint* introduces the immediately-declared constraint of the *type-constraint* for the parameter. [*Example 2*:

```cpp
template<typename T> concept C1 = true;
template<typename... Ts> concept C2 = true;
template<typename T, typename U> concept C3 = true;

template<C1 T> struct s1;               // associates C1<T>
template<C1... T> struct s2;            // associates (C1<T> && ...)
template<C2... T> struct s3;            // associates (C2<T> && ...)
template<C3<int> T> struct s4;          // associates C3<T, int>
template<C3<int>... T> struct s5;       // associates (C3<T, int> && ...)
```

— *end example*]

**6** A non-type *template-parameter* shall have one of the following (possibly cv-qualified) types:

- a structural type (see below),
- a type that contains a placeholder type ([[dcl.spec.auto]](dcl.spec.auto.md)), or
- a placeholder for a deduced class type ([[dcl.type.class.deduct]](dcl.type.class.deduct.md)).

The top-level *cv-qualifier*s on the *template-parameter* are ignored when determining its type.

**7** A *structural type* is one of the following:

- a scalar type, or

- an lvalue reference type, or

- a literal class type with the following properties:

  - all base classes and non-static data members are public and non-mutable and
  - the types of all bases classes and non-static data members are structural types or (possibly multi-dimensional) array thereof.

**8** An *id-expression* naming a non-type *template-parameter* of class type `T` denotes a static storage duration object of type `const T`, known as a *template parameter object*, whose value is that of the corresponding template argument after it has been converted to the type of the *template-parameter*. All such template parameters in the program of the same type with the same value denote the same template parameter object. A template parameter object shall have constant destruction ([[expr.const]](expr.const.md)). [*Note 3*: If an *id-expression* names a non-type non-reference *template-parameter*, then it is a prvalue if it has non-class type. Otherwise, if it is of class type `T`, it is an lvalue and has type `const T` ([[expr.prim.id.unqual]](expr.prim.id.unqual.md)). — *end note*] [*Example 3*:

```cpp
using X = int;
struct A {};
template<const X& x, int i, A a> void f() {
  i++;                          // error: change of template-parameter value

  &x;                           // OK
  &i;                           // error: address of non-reference template-parameter
  &a;                           // OK
  int& ri = i;                  // error: non-const reference bound to temporary
  const int& cri = i;           // OK: const reference bound to temporary
  const A& ra = a;              // OK: const reference bound to a template parameter object
}
```

— *end example*]

**9** [*Note 4*: A non-type *template-parameter* cannot be declared to have type *cv* `void`. [*Example 4*:

```cpp
template<void v> class X;       // error
template<void* pv> class Y;     // OK
```

— *end example*] — *end note*]

**10** A non-type *template-parameter* of type “array of `T`” or of function type `T` is adjusted to be of type “pointer to `T`”. [*Example 5*:

```cpp
template<int* a>   struct R { /* ... */ };
template<int b[5]> struct S { /* ... */ };
int p;
R<&p> w;                        // OK
S<&p> x;                        // OK due to parameter adjustment
int v[5];
R<v> y;                         // OK due to implicit argument conversion
S<v> z;                         // OK due to both adjustment and conversion
```

— *end example*]

**11** A non-type template parameter declared with a type that contains a placeholder type with a *type-constraint* introduces the immediately-declared constraint of the *type-constraint* for the invented type corresponding to the placeholder ([[dcl.fct]](dcl.fct.md)).

**12** A *default template-argument* is a *template-argument* ([[temp.arg]](temp.arg.md)) specified after `=` in a *template-parameter*. A default *template-argument* may be specified for any kind of *template-parameter* (type, non-type, template) that is not a template parameter pack ([[temp.variadic]](temp.variadic.md)). A default *template-argument* may be specified in a template declaration. A default *template-argument* shall not be specified in the *template-parameter-list*s of the definition of a member of a class template that appears outside of the member’s class. A default *template-argument* shall not be specified in a friend class template declaration. If a friend function template declaration specifies a default *template-argument*, that declaration shall be a definition and shall be the only declaration of the function template in the translation unit.

**13** The set of default *template-argument*s available for use is obtained by merging the default arguments from all prior declarations of the template in the same way default function arguments are ([[dcl.fct.default]](dcl.fct.default.md)). [*Example 6*:

```cpp
template<class T1, class T2 = int> class A;
template<class T1 = int, class T2> class A;
```

is equivalent to

```cpp
template<class T1 = int, class T2 = int> class A;
```

— *end example*]

**14** If a *template-parameter* of a class template, variable template, or alias template has a default *template-argument*, each subsequent *template-parameter* shall either have a default *template-argument* supplied or be a template parameter pack. If a *template-parameter* of a primary class template, primary variable template, or alias template is a template parameter pack, it shall be the last *template-parameter*. A template parameter pack of a function template shall not be followed by another template parameter unless that template parameter can be deduced from the parameter-type-list ([[dcl.fct]](dcl.fct.md)) of the function template or has a default argument ([[temp.deduct]](temp.deduct.md)). A template parameter of a deduction guide template ([[temp.deduct.guide]](temp.deduct.guide.md)) that does not have a default argument shall be deducible from the parameter-type-list of the deduction guide template. [*Example 7*:

```cpp
template<class T1 = int, class T2> class B;     // error

// U can be neither deduced from the parameter-type-list nor specified
template<class... T, class... U> void f() { }   // error
template<class... T, class U> void g() { }      // error
```

— *end example*]

**15** A *template-parameter* shall not be given default arguments by two different declarations in the same scope. [*Example 8*:

```cpp
template<class T = int> class X;
template<class T = int> class X { /* ... */ };  // error
```

— *end example*]

**16** When parsing a default *template-argument* for a non-type *template-parameter*, the first non-nested `>` is taken as the end of the *template-parameter-list* rather than a greater-than operator. [*Example 9*:

```cpp
template<int i = 3 > 4 >        // syntax error
class X { /* ... */ };

template<int i = (3 > 4) >      // OK
class Y { /* ... */ };
```

— *end example*]

**17** A *template-parameter* of a template *template-parameter* is permitted to have a default *template-argument*. When such default arguments are specified, they apply to the template *template-parameter* in the scope of the template *template-parameter*. [*Example 10*:

```cpp
template <template <class TT = float> class T> struct A {
  inline void f();
  inline void g();
};
template <template <class TT> class T> void A<T>::f() {
  T<> t;            // error: TT has no default template argument
}
template <template <class TT = char> class T> void A<T>::g() {
  T<> t;            // OK, T<char>
}
```

— *end example*]

**18** If a *template-parameter* is a *type-parameter* with an ellipsis prior to its optional *identifier* or is a *parameter-declaration* that declares a pack ([[dcl.fct]](dcl.fct.md)), then the *template-parameter* is a template parameter pack ([[temp.variadic]](temp.variadic.md)). A template parameter pack that is a *parameter-declaration* whose type contains one or more unexpanded packs is a pack expansion. Similarly, a template parameter pack that is a *type-parameter* with a *template-parameter-list* containing one or more unexpanded packs is a pack expansion. A type parameter pack with a *type-constraint* that contains an unexpanded parameter pack is a pack expansion. A template parameter pack that is a pack expansion shall not expand a template parameter pack declared in the same *template-parameter-list*. [*Example 11*:

```cpp
template <class... Types>                       // Types is a template type parameter pack
   class Tuple;                                 // but not a pack expansion

template <class T, int... Dims>                 // Dims is a non-type template parameter pack
   struct multi_array;                          // but not a pack expansion

template <class... T>
  struct value_holder {
    template <T... Values> struct apply { };    // Values is a non-type template parameter pack
  };                                            // and a pack expansion

template <class... T, T... Values>              // error: Values expands template type parameter
  struct static_array;                          // pack T within the same template parameter list
```

— *end example*]

[^1]: Since template *template-parameter*s and template *template-argument*s are treated as types for descriptive purposes, the terms *non-type parameter* and *non-type argument* are used to refer to non-type, non-template parameters and arguments.
