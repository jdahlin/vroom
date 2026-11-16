# 12.3 Declaration matching [over.dcl]

[Contents](../README.md) · up: [12 Overloading \[over\]](over.md) · prev: [12.2 Overloadable declarations \[over.load\]](over.load.md) · next: [12.4 Overload resolution \[over.match\]](over.match.md)

**1** Two function declarations of the same name refer to the same function if they are in the same scope and have equivalent parameter declarations ([[over.load]](over.load.md)) and equivalent ([[temp.over.link]](../temp/temp.over.link.md)) trailing *requires-clause*s, if any ([[dcl.decl]](../dcl/dcl.decl.md)). [*Note 1*: Since a *constraint-expression* is an unevaluated operand, equivalence compares the expressions without evaluating them. [*Example 1*:

```cpp
template<int I> concept C = true;
template<typename T> struct A {
  void f() requires C<42>;      // #1
  void f() requires true;       // OK, different functions
};
```

— *end example*] — *end note*] A function member of a derived class is *not* in the same scope as a function member of the same name in a base class. [*Example 2*:

```cpp
struct B {
  int f(int);
};

struct D : B {
  int f(const char*);
};
```

Here `D::f(const char*)` hides `B::f(int)` rather than overloading it.

```cpp
void h(D* pd) {
  pd->f(1);                     // error:
                                // D::f(const char*) hides B::f(int)
  pd->B::f(1);                  // OK
  pd->f("Ben");                 // OK, calls D::f
}
```

— *end example*]

**2** A locally declared function is not in the same scope as a function in a containing scope. [*Example 3*:

```cpp
void f(const char*);
void g() {
  extern void f(int);
  f("asdf");                    // error: f(int) hides f(const char*)
                                // so there is no f(const char*) in this scope
}

void caller () {
  extern void callee(int, int);
  {
    extern void callee(int);    // hides callee(int, int)
    callee(88, 99);             // error: only callee(int) in scope
  }
}
```

— *end example*]

**3** Different versions of an overloaded member function can be given different access rules. [*Example 4*:

```cpp
class buffer {
private:
    char* p;
    int size;
protected:
    buffer(int s, char* store) { size = s; p = store; }
public:
    buffer(int s) { p = new char[size = s]; }
};
```

— *end example*]
