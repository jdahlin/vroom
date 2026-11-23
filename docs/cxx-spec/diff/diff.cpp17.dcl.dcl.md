# C.1.4 [[dcl.dcl]](../dcl/dcl.dcl.md): declarations [diff.cpp17.dcl.dcl]

[Contents](../README.md) · up: [C.1 C++ and ISO C++ 2017 \[diff.cpp17\]](diff.cpp17.md) · prev: [C.1.3 [[expr]](../expr/expr.md): expressions \[diff.cpp17.expr\]](diff.cpp17.expr.md) · next: [C.1.5 [[class]](../class/class.md): classes \[diff.cpp17.class\]](diff.cpp17.class.md)

**1** **Affected subclause:** [[dcl.typedef]](../dcl/dcl.typedef.md) <br>**Change:** Unnamed classes with a typedef name for linkage purposes can contain only C-compatible constructs. <br>**Rationale:** Necessary for implementability. <br>**Effect on original feature:** Valid C++ 2017 code may be ill-formed in this International Standard.

```cpp
typedef struct {
  void f() {}           // ill-formed; previously well-formed
} S;
```

**2** **Affected subclause:** [[dcl.fct.default]](../dcl/dcl.fct.default.md) <br>**Change:** A function cannot have different default arguments in different translation units. <br>**Rationale:** Required for modules support. <br>**Effect on original feature:** Valid C++ 2017 code may be ill-formed in this International Standard, with no diagnostic required.

```cpp
// Translation unit 1
int f(int a = 42);
int g() { return f(); }

// Translation unit 2
int f(int a = 76) { return a; }         // ill-formed, no diagnostic required; previously well-formed
int g();
int main() { return g(); }              // used to return 42
```

**3** **Affected subclause:** [[dcl.init.aggr]](../dcl/dcl.init.aggr.md) <br>**Change:** A class that has user-declared constructors is never an aggregate. <br>**Rationale:** Remove potentially error-prone aggregate initialization which may apply notwithstanding the declared constructors of a class. <br>**Effect on original feature:** Valid C++ 2017 code that aggregate-initializes a type with a user-declared constructor may be ill-formed or have different semantics in this International Standard.

```cpp
struct A {              // not an aggregate; previously an aggregate
  A() = delete;
};

struct B {              // not an aggregate; previously an aggregate
  B() = default;
  int i = 0;
};

struct C {              // not an aggregate; previously an aggregate
  C(C&&) = default;
  int a, b;
};

A a{};                  // ill-formed; previously well-formed
B b = {1};              // ill-formed; previously well-formed
auto* c = new C{2, 3};  // ill-formed; previously well-formed

struct Y;

struct X {
  operator Y();
};

struct Y {              // not an aggregate; previously an aggregate
  Y(const Y&) = default;
  X x;
};

Y y{X{}};               // copy constructor call; previously aggregate-initialization
```

**4** **Affected subclause:** [[dcl.init.list]](../dcl/dcl.init.list.md) <br>**Change:** Boolean conversion from a pointer or pointer-to-member type is now a narrowing conversion. <br>**Rationale:** Catches bugs. <br>**Effect on original feature:** Valid C++ 2017 code may fail to compile in this International Standard. For example:

```cpp
bool y[] = { "bc" };    // ill-formed; previously well-formed
```
