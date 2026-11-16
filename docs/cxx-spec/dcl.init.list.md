# 9.4.4 List-initialization [dcl.init.list]

[Contents](README.md) · up: [9.4 Initializers \[dcl.init\]](dcl.init.md) · prev: [9.4.3 References \[dcl.init.ref\]](dcl.init.ref.md) · next: [9.5 Function definitions \[dcl.fct.def\]](dcl.fct.def.md)

**1** *List-initialization* is initialization of an object or reference from a *braced-init-list*. Such an initializer is called an *initializer list*, and the comma-separated *initializer-clause*s of the *initializer-list* or *designated-initializer-clause*s of the *designated-initializer-list* are called the *elements* of the initializer list. An initializer list may be empty. List-initialization can occur in direct-initialization or copy-initialization contexts; list-initialization in a direct-initialization context is called *direct-list-initialization* and list-initialization in a copy-initialization context is called *copy-list-initialization*. [*Note 1*: List-initialization can be used

- as the initializer in a variable definition ([[dcl.init]](dcl.init.md))
- as the initializer in a *new-expression* ([[expr.new]](expr.new.md))
- in a `return` statement ([[stmt.return]](stmt.return.md))
- as a *for-range-initializer* ([[stmt.iter]](stmt.iter.md))
- as a function argument ([[expr.call]](expr.call.md))
- as a subscript ([[expr.sub]](expr.sub.md))
- as an argument to a constructor invocation ([[dcl.init]](dcl.init.md), [[expr.type.conv]](expr.type.conv.md))
- as an initializer for a non-static data member ([[class.mem]](class.mem.md))
- in a *mem-initializer* ([[class.base.init]](class.base.init.md))
- on the right-hand side of an assignment ([[expr.ass]](expr.ass.md))

[*Example 1*:

```cpp
int a = {1};
std::complex<double> z{1,2};
new std::vector<std::string>{"once", "upon", "a", "time"};  // 4 string elements
f( {"Nicholas","Annemarie"} );  // pass list of two elements
return { "Norah" };             // return list of one element
int* e {};                      // initialization to zero / null pointer
x = double{1};                  // explicitly construct a double
std::map<std::string,int> anim = { {"bear",4}, {"cassowary",2}, {"tiger",7} };
```

— *end example*] — *end note*]

**2** A constructor is an *initializer-list constructor* if its first parameter is of type `std::initializer_list<E>` or reference to *cv* `std::initializer_list<E>` for some type `E`, and either there are no other parameters or else all other parameters have default arguments ([[dcl.fct.default]](dcl.fct.default.md)). [*Note 2*: Initializer-list constructors are favored over other constructors in list-initialization ([[over.match.list]](over.match.list.md)). Passing an initializer list as the argument to the constructor template `template<class T> C(T)` of a class `C` does not create an initializer-list constructor, because an initializer list argument causes the corresponding parameter to be a non-deduced context ([[temp.deduct.call]](temp.deduct.call.md)). — *end note*] The template `std::initializer_list` is not predefined; if the header `<initializer_list>` is not imported or included prior to a use of `std::initializer_list` — even an implicit use in which the type is not named ([[dcl.spec.auto]](dcl.spec.auto.md)) — the program is ill-formed.

**3** List-initialization of an object or reference of type `T` is defined as follows:

- If the *braced-init-list* contains a *designated-initializer-list*, `T` shall be an aggregate class. The ordered *identifier*s in the *designator*s of the *designated-initializer-list* shall form a subsequence of the ordered *identifier*s in the direct non-static data members of `T`. Aggregate initialization is performed ([[dcl.init.aggr]](dcl.init.aggr.md)). [*Example 2*:

  ```cpp
  struct A { int x; int y; int z; };
  A a{.y = 2, .x = 1};                // error: designator order does not match declaration order
  A b{.x = 1, .z = 2};                // OK, b.y initialized to 0
  ```

  — *end example*]

- If `T` is an aggregate class and the initializer list has a single element of type *cv* `U`, where `U` is `T` or a class derived from `T`, the object is initialized from that element (by copy-initialization for copy-list-initialization, or by direct-initialization for direct-list-initialization).

- Otherwise, if `T` is a character array and the initializer list has a single element that is an appropriately-typed *string-literal* ([[dcl.init.string]](dcl.init.string.md)), initialization is performed as described in that subclause.

- Otherwise, if `T` is an aggregate, aggregate initialization is performed ([[dcl.init.aggr]](dcl.init.aggr.md)).

  [*Example 3*:

  ```cpp
  double ad[] = { 1, 2.0 };           // OK
  int ai[] = { 1, 2.0 };              // error: narrowing

  struct S2 {
    int m1;
    double m2, m3;
  };
  S2 s21 = { 1, 2, 3.0 };             // OK
  S2 s22 { 1.0, 2, 3 };               // error: narrowing
  S2 s23 { };                         // OK: default to 0,0,0
  ```

  — *end example*]

- Otherwise, if the initializer list has no elements and `T` is a class type with a default constructor, the object is value-initialized.

- Otherwise, if `T` is a specialization of `std::initializer_list<E>`, the object is constructed as described below.

- Otherwise, if `T` is a class type, constructors are considered. The applicable constructors are enumerated and the best one is chosen through overload resolution ([[over.match]](over.match.md), [[over.match.list]](over.match.list.md)). If a narrowing conversion (see below) is required to convert any of the arguments, the program is ill-formed.

  [*Example 4*:

  ```cpp
  struct S {
    S(std::initializer_list<double>); // #1
    S(std::initializer_list<int>);    // #2
    S();                              // #3
    // ...
  };
  S s1 = { 1.0, 2.0, 3.0 };           // invoke #1
  S s2 = { 1, 2, 3 };                 // invoke #2
  S s3 = { };                         // invoke #3
  ```

  — *end example*]

  [*Example 5*:

  ```cpp
  struct Map {
    Map(std::initializer_list<std::pair<std::string,int>>);
  };
  Map ship = {{"Sophie",14}, {"Surprise",28}};
  ```

  — *end example*]

  [*Example 6*:

  ```cpp
  struct S {
    // no initializer-list constructors
    S(int, double, double);           // #1
    S();                              // #2
    // ...
  };
  S s1 = { 1, 2, 3.0 };               // OK: invoke #1
  S s2 { 1.0, 2, 3 };                 // error: narrowing
  S s3 { };                           // OK: invoke #2
  ```

  — *end example*]

- Otherwise, if `T` is an enumeration with a fixed underlying type ([[dcl.enum]](dcl.enum.md)) `U`, the *initializer-list* has a single element `v`, `v` can be implicitly converted to `U`, and the initialization is direct-list-initialization, the object is initialized with the value `T(v)` ([[expr.type.conv]](expr.type.conv.md)); if a narrowing conversion is required to convert `v` to `U`, the program is ill-formed. [*Example 7*:

  ```cpp
  enum byte : unsigned char { };
  byte b { 42 };                      // OK
  byte c = { 42 };                    // error
  byte d = byte{ 42 };                // OK; same value as b
  byte e { -1 };                      // error

  struct A { byte b; };
  A a1 = { { 42 } };                  // error
  A a2 = { byte{ 42 } };              // OK

  void f(byte);
  f({ 42 });                          // error

  enum class Handle : uint32_t { Invalid = 0 };
  Handle h { 42 };                    // OK
  ```

  — *end example*]

- Otherwise, if the initializer list has a single element of type `E` and either `T` is not a reference type or its referenced type is reference-related to `E`, the object or reference is initialized from that element (by copy-initialization for copy-list-initialization, or by direct-initialization for direct-list-initialization); if a narrowing conversion (see below) is required to convert the element to `T`, the program is ill-formed.

  [*Example 8*:

  ```cpp
  int x1 {2};                         // OK
  int x2 {2.0};                       // error: narrowing
  ```

  — *end example*]

- Otherwise, if `T` is a reference type, a prvalue is generated. The prvalue initializes its result object by copy-list-initialization. The prvalue is then used to direct-initialize the reference. The type of the temporary is the type referenced by `T`, unless `T` is “reference to array of unknown bound of `U`”, in which case the type of the temporary is the type of `x` in the declaration `U x[] H`, where *H* is the initializer list. [*Note 3*: As usual, the binding will fail and the program is ill-formed if the reference type is an lvalue reference to a non-const type. — *end note*]

  [*Example 9*:

  ```cpp
  struct S {
    S(std::initializer_list<double>); // #1
    S(const std::string&);            // #2
    // ...
  };
  const S& r1 = { 1, 2, 3.0 };        // OK: invoke #1
  const S& r2 { "Spinach" };          // OK: invoke #2
  S& r3 = { 1, 2, 3 };                // error: initializer is not an lvalue
  const int& i1 = { 1 };              // OK
  const int& i2 = { 1.1 };            // error: narrowing
  const int (&iar)[2] = { 1, 2 };     // OK: iar is bound to temporary array

  struct A { } a;
  struct B { explicit B(const A&); };
  const B& b2{a};                     // error: cannot copy-list-initialize B temporary from A
  ```

  — *end example*]

- Otherwise, if the initializer list has no elements, the object is value-initialized.

  [*Example 10*:

  ```cpp
  int** pp {};                        // initialized to null pointer
  ```

  — *end example*]

- Otherwise, the program is ill-formed.

  [*Example 11*:

  ```cpp
  struct A { int i; int j; };
  A a1 { 1, 2 };                      // aggregate initialization
  A a2 { 1.2 };                       // error: narrowing
  struct B {
    B(std::initializer_list<int>);
  };
  B b1 { 1, 2 };                      // creates initializer_list<int> and calls constructor
  B b2 { 1, 2.0 };                    // error: narrowing
  struct C {
    C(int i, double j);
  };
  C c1 = { 1, 2.2 };                  // calls constructor with arguments (1, 2.2)
  C c2 = { 1.1, 2 };                  // error: narrowing

  int j { 1 };                        // initialize to 1
  int k { };                          // initialize to 0
  ```

  — *end example*]

**4** Within the *initializer-list* of a *braced-init-list*, the *initializer-clause*s, including any that result from pack expansions ([[temp.variadic]](temp.variadic.md)), are evaluated in the order in which they appear. That is, every value computation and side effect associated with a given *initializer-clause* is sequenced before every value computation and side effect associated with any *initializer-clause* that follows it in the comma-separated list of the *initializer-list*. [*Note 4*: This evaluation ordering holds regardless of the semantics of the initialization; for example, it applies when the elements of the *initializer-list* are interpreted as arguments of a constructor call, even though ordinarily there are no sequencing constraints on the arguments of a call. — *end note*]

**5** An object of type `std::initializer_list<E>` is constructed from an initializer list as if the implementation generated and materialized ([[conv.rval]](conv.rval.md)) a prvalue of type “array of *N* `const E`”, where *N* is the number of elements in the initializer list. Each element of that array is copy-initialized with the corresponding element of the initializer list, and the `std::initializer_list<E>` object is constructed to refer to that array. [*Note 5*: A constructor or conversion function selected for the copy is required to be accessible ([[class.access]](class.access.md)) in the context of the initializer list. — *end note*] If a narrowing conversion is required to initialize any of the elements, the program is ill-formed. [*Example 12*:

```cpp
struct X {
  X(std::initializer_list<double> v);
};
X x{ 1,2,3 };
```

The initialization will be implemented in a way roughly equivalent to this:

```cpp
const double __a[3] = {double{1}, double{2}, double{3}};
X x(std::initializer_list<double>(__a, __a+3));
```

assuming that the implementation can construct an `initializer_list` object with a pair of pointers. — *end example*]

**6** The array has the same lifetime as any other temporary object ([[class.temporary]](class.temporary.md)), except that initializing an `initializer_list` object from the array extends the lifetime of the array exactly like binding a reference to a temporary. [*Example 13*:

```cpp
typedef std::complex<double> cmplx;
std::vector<cmplx> v1 = { 1, 2, 3 };

void f() {
  std::vector<cmplx> v2{ 1, 2, 3 };
  std::initializer_list<int> i3 = { 1, 2, 3 };
}

struct A {
  std::initializer_list<int> i4;
  A() : i4{ 1, 2, 3 } {}            // ill-formed, would create a dangling reference
};
```

For `v1` and `v2`, the `initializer_list` object is a parameter in a function call, so the array created for `{ 1, 2, 3 }` has full-expression lifetime. For `i3`, the `initializer_list` object is a variable, so the array persists for the lifetime of the variable. For `i4`, the `initializer_list` object is initialized in the constructor’s *ctor-initializer* as if by binding a temporary array to a reference member, so the program is ill-formed ([[class.base.init]](class.base.init.md)). — *end example*] [*Note 6*: The implementation is free to allocate the array in read-only memory if an explicit array with the same initializer could be so allocated. — *end note*]

**7** A *narrowing conversion* is an implicit conversion

- from a floating-point type to an integer type, or
- from `long double` to `double` or `float`, or from `double` to `float`, except where the source is a constant expression and the actual value after conversion is within the range of values that can be represented (even if it cannot be represented exactly), or
- from an integer type or unscoped enumeration type to a floating-point type, except where the source is a constant expression and the actual value after conversion will fit into the target type and will produce the original value when converted back to the original type, or
- from an integer type or unscoped enumeration type to an integer type that cannot represent all the values of the original type, except where the source is a constant expression whose value after integral promotions will fit into the target type, or
- from a pointer type or a pointer-to-member type to `bool`.

[*Note 7*: As indicated above, such conversions are not allowed at the top level in list-initializations. — *end note*] [*Example 14*:

```cpp
int x = 999;                    // x is not a constant expression
const int y = 999;
const int z = 99;
char c1 = x;                    // OK, though it might narrow (in this case, it does narrow)
char c2{x};                     // error: might narrow
char c3{y};                     // error: narrows (assuming char is 8 bits)
char c4{z};                     // OK: no narrowing needed
unsigned char uc1 = {5};        // OK: no narrowing needed
unsigned char uc2 = {-1};       // error: narrows
unsigned int ui1 = {-1};        // error: narrows
signed int si1 =
  { (unsigned int)-1 };         // error: narrows
int ii = {2.0};                 // error: narrows
float f1 { x };                 // error: might narrow
float f2 { 7 };                 // OK: 7 can be exactly represented as a float
bool b = {"meow"};              // error: narrows
int f(int);
int a[] = { 2, f(2), f(2.0) };  // OK: the double-to-int conversion is not at the top level
```

— *end example*]
