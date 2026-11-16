# 12.4.3.1.5 List-initialization sequence [over.ics.list]

[Contents](README.md) · up: [12.4.3.1 Implicit conversion sequences \[over.best.ics\]](over.best.ics.md) · prev: [12.4.3.1.4 Reference binding \[over.ics.ref\]](over.ics.ref.md) · next: [12.4.3.2 Ranking implicit conversion sequences \[over.ics.rank\]](over.ics.rank.md)

**1** When an argument is an initializer list ([[dcl.init.list]](dcl.init.list.md)), it is not an expression and special rules apply for converting it to a parameter type.

**2** If the initializer list is a *designated-initializer-list*, a conversion is only possible if the parameter has an aggregate type that can be initialized from the initializer list according to the rules for aggregate initialization ([[dcl.init.aggr]](dcl.init.aggr.md)), in which case the implicit conversion sequence is a user-defined conversion sequence whose second standard conversion sequence is an identity conversion. [*Note 1*: Aggregate initialization does not require that the members are declared in designation order. If, after overload resolution, the order does not match for the selected overload, the initialization of the parameter will be ill-formed ([[dcl.init.list]](dcl.init.list.md)). [*Example 1*:

```cpp
struct A { int x, y; };
struct B { int y, x; };
void f(A a, int);               // #1
void f(B b, ...);               // #2
void g(A a);                    // #3
void g(B b);                    // #4
void h() {
  f({.x = 1, .y = 2}, 0);       // OK; calls #1
  f({.y = 2, .x = 1}, 0);       // error: selects #1, initialization of a fails
                                // due to non-matching member order ([dcl.init.list])
  g({.x = 1, .y = 2});          // error: ambiguous between #3 and #4
}
```

— *end example*] — *end note*]

**3** Otherwise, if the parameter type is an aggregate class `X` and the initializer list has a single element of type *cv* `U`, where `U` is `X` or a class derived from `X`, the implicit conversion sequence is the one required to convert the element to the parameter type.

**4** Otherwise, if the parameter type is a character array[^1] and the initializer list has a single element that is an appropriately-typed *string-literal* ([[dcl.init.string]](dcl.init.string.md)), the implicit conversion sequence is the identity conversion.

**5** Otherwise, if the parameter type is `std::initializer_list<X>` and all the elements of the initializer list can be implicitly converted to `X`, the implicit conversion sequence is the worst conversion necessary to convert an element of the list to `X`, or if the initializer list has no elements, the identity conversion. This conversion can be a user-defined conversion even in the context of a call to an initializer-list constructor. [*Example 2*:

```cpp
void f(std::initializer_list<int>);
f( {} );                        // OK: f(initializer_list<int>) identity conversion
f( {1,2,3} );                   // OK: f(initializer_list<int>) identity conversion
f( {'a','b'} );                 // OK: f(initializer_list<int>) integral promotion
f( {1.0} );                     // error: narrowing

struct A {
  A(std::initializer_list<double>);             // #1
  A(std::initializer_list<complex<double>>);    // #2
  A(std::initializer_list<std::string>);        // #3
};
A a{ 1.0,2.0 };                 // OK, uses #1

void g(A);
g({ "foo", "bar" });            // OK, uses #3

typedef int IA[3];
void h(const IA&);
h({ 1, 2, 3 });                 // OK: identity conversion
```

— *end example*]

**6** Otherwise, if the parameter type is “array of `N` `X`” or “array of unknown bound of `X`”, if there exists an implicit conversion sequence from each element of the initializer list (and from `{}` in the former case if `N` exceeds the number of elements in the initializer list) to `X`, the implicit conversion sequence is the worst such implicit conversion sequence.

**7** Otherwise, if the parameter is a non-aggregate class `X` and overload resolution per [[over.match.list]](over.match.list.md) chooses a single best constructor `C` of `X` to perform the initialization of an object of type `X` from the argument initializer list:

- If `C` is not an initializer-list constructor and the initializer list has a single element of type *cv* `U`, where `U` is `X` or a class derived from `X`, the implicit conversion sequence has Exact Match rank if `U` is `X`, or Conversion rank if `U` is derived from `X`.
- Otherwise, the implicit conversion sequence is a user-defined conversion sequence with the second standard conversion sequence an identity conversion.

If multiple constructors are viable but none is better than the others, the implicit conversion sequence is the ambiguous conversion sequence. User-defined conversions are allowed for conversion of the initializer list elements to the constructor parameter types except as noted in [[over.best.ics]](over.best.ics.md). [*Example 3*:

```cpp
struct A {
  A(std::initializer_list<int>);
};
void f(A);
f( {'a', 'b'} );        // OK: f(A(std::initializer_list<int>)) user-defined conversion

struct B {
  B(int, double);
};
void g(B);
g( {'a', 'b'} );        // OK: g(B(int, double)) user-defined conversion
g( {1.0, 1.0} );        // error: narrowing

void f(B);
f( {'a', 'b'} );        // error: ambiguous f(A) or f(B)

struct C {
  C(std::string);
};
void h(C);
h({"foo"});             // OK: h(C(std::string("foo")))

struct D {
  D(A, C);
};
void i(D);
i({ {1,2}, {"bar"} });  // OK: i(D(A(std::initializer_list<int>{1,2}), C(std::string("bar"))))
```

— *end example*]

**8** Otherwise, if the parameter has an aggregate type which can be initialized from the initializer list according to the rules for aggregate initialization ([[dcl.init.aggr]](dcl.init.aggr.md)), the implicit conversion sequence is a user-defined conversion sequence with the second standard conversion sequence an identity conversion. [*Example 4*:

```cpp
struct A {
  int m1;
  double m2;
};

void f(A);
f( {'a', 'b'} );        // OK: f(A(int,double)) user-defined conversion
f( {1.0} );             // error: narrowing
```

— *end example*]

**9** Otherwise, if the parameter is a reference, see [[over.ics.ref]](over.ics.ref.md). [*Note 2*: The rules in this subclause will apply for initializing the underlying temporary for the reference. — *end note*] [*Example 5*:

```cpp
struct A {
  int m1;
  double m2;
};

void f(const A&);
f( {'a', 'b'} );        // OK: f(A(int,double)) user-defined conversion
f( {1.0} );             // error: narrowing

void g(const double &);
g({1});                 // same conversion as int to double
```

— *end example*]

**10** Otherwise, if the parameter type is not a class:

- if the initializer list has one element that is not itself an initializer list, the implicit conversion sequence is the one required to convert the element to the parameter type; [*Example 6*:

  ```cpp
  void f(int);
  f( {'a'} );             // OK: same conversion as char to int
  f( {1.0} );             // error: narrowing
  ```

  — *end example*]

- if the initializer list has no elements, the implicit conversion sequence is the identity conversion. [*Example 7*:

  ```cpp
  void f(int);
  f( { } );               // OK: identity conversion
  ```

  — *end example*]

**11** In all cases other than those enumerated above, no conversion is possible.

[^1]: Since there are no parameters of array type, this will only occur as the referenced type of a reference parameter.
