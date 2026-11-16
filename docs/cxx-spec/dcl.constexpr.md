# 9.2.5 The `constexpr` and `consteval` specifiers [dcl.constexpr]

[Contents](README.md) · up: [9.2 Specifiers \[dcl.spec\]](dcl.spec.md) · prev: [9.2.4 The `friend` specifier \[dcl.friend\]](dcl.friend.md) · next: [9.2.6 The `constinit` specifier \[dcl.constinit\]](dcl.constinit.md)

**1** The `constexpr` specifier shall be applied only to the definition of a variable or variable template or the declaration of a function or function template. The `consteval` specifier shall be applied only to the declaration of a function or function template. A function or static data member declared with the `constexpr` or `consteval` specifier is implicitly an inline function or variable ([[dcl.inline]](dcl.inline.md)). If any declaration of a function or function template has a `constexpr` or `consteval` specifier, then all its declarations shall contain the same specifier. [*Note 1*: An explicit specialization can differ from the template declaration with respect to the `constexpr` or `consteval` specifier. — *end note*] [*Note 2*: Function parameters cannot be declared `constexpr`. — *end note*] [*Example 1*:

```cpp
constexpr void square(int &x);  // OK: declaration
constexpr int bufsz = 1024;     // OK: definition
constexpr struct pixel {        // error: pixel is a type
  int x;
  int y;
  constexpr pixel(int);         // OK: declaration
};
constexpr pixel::pixel(int a)
  : x(a), y(x)                  // OK: definition
  { square(x); }
constexpr pixel small(2);       // error: square not defined, so small(2)
                                // not constant ([expr.const]) so constexpr not satisfied

constexpr void square(int &x) { // OK: definition
  x *= x;
}
constexpr pixel large(4);       // OK: square defined
int next(constexpr int x) {     // error: not for parameters
     return x + 1;
}
extern constexpr int memsz;     // error: not a definition
```

— *end example*]

**2** A `constexpr` or `consteval` specifier used in the declaration of a function declares that function to be a *constexpr function*. A function or constructor declared with the `consteval` specifier is called an *immediate function*. A destructor, an allocation function, or a deallocation function shall not be declared with the `consteval` specifier.

**3** The definition of a constexpr function shall satisfy the following requirements:

- its return type (if any) shall be a literal type;

- each of its parameter types shall be a literal type;

- it shall not be a coroutine ([[dcl.fct.def.coroutine]](dcl.fct.def.coroutine.md));

- if the function is a constructor or destructor, its class shall not have any virtual base classes;

- its *function-body* shall not enclose ([[stmt.pre]](stmt.pre.md))

  - a `goto` statement,
  - an identifier label ([[stmt.label]](stmt.label.md)),
  - a definition of a variable of non-literal type or of static or thread storage duration.

  [*Note 3*: A *function-body* that is `= delete` or `= default` encloses none of the above. — *end note*]

[*Example 2*:

```cpp
constexpr int square(int x)
  { return x * x; }             // OK
constexpr long long_max()
  { return 2147483647; }        // OK
constexpr int abs(int x) {
  if (x < 0)
    x = -x;
  return x;                     // OK
}
constexpr int first(int n) {
  static int value = n;         // error: variable has static storage duration
  return value;
}
constexpr int uninit() {
  struct { int a; } s;
  return s.a;                   // error: uninitialized read of s.a
}
constexpr int prev(int x)
  { return --x; }               // OK
constexpr int g(int x, int n) { // OK
  int r = 1;
  while (--n > 0) r *= x;
  return r;
}
```

— *end example*]

**4** The definition of a constexpr constructor whose *function-body* is not `= delete` shall additionally satisfy the following requirements:

- for a non-delegating constructor, every constructor selected to initialize non-static data members and base class subobjects shall be a constexpr constructor;
- for a delegating constructor, the target constructor shall be a constexpr constructor.

[*Example 3*:

```cpp
struct Length {
  constexpr explicit Length(int i = 0) : val(i) { }
private:
  int val;
};
```

— *end example*]

**5** The definition of a constexpr destructor whose *function-body* is not `= delete` shall additionally satisfy the following requirement:

- for every subobject of class type or (possibly multi-dimensional) array thereof, that class type shall have a constexpr destructor.

**6** For a constexpr function or constexpr constructor that is neither defaulted nor a template, if no argument values exist such that an invocation of the function or constructor could be an evaluated subexpression of a core constant expression ([[expr.const]](expr.const.md)), or, for a constructor, an evaluated subexpression of the initialization full-expression of some constant-initialized object ([[basic.start.static]](basic.start.static.md)), the program is ill-formed, no diagnostic required. [*Example 4*:

```cpp
constexpr int f(bool b)
  { return b ? throw 0 : 0; }           // OK
constexpr int f() { return f(true); }   // ill-formed, no diagnostic required

struct B {
  constexpr B(int x) : i(0) { }         // x is unused
  int i;
};

int global;

struct D : B {
  constexpr D() : B(global) { }         // ill-formed, no diagnostic required
                                        // lvalue-to-rvalue conversion on non-constant global
};
```

— *end example*]

**7** If the instantiated template specialization of a constexpr function template or member function of a class template would fail to satisfy the requirements for a constexpr function, that specialization is still a constexpr function, even though a call to such a function cannot appear in a constant expression. If no specialization of the template would satisfy the requirements for a constexpr function when considered as a non-template function, the template is ill-formed, no diagnostic required.

**8** An invocation of a constexpr function in a given context produces the same result as an invocation of an equivalent non-constexpr function in the same context in all respects except that

- an invocation of a constexpr function can appear in a constant expression ([[expr.const]](expr.const.md)) and
- copy elision is not performed in a constant expression ([[class.copy.elision]](class.copy.elision.md)).

[*Note 4*: Declaring a function constexpr can change whether an expression is a constant expression. This can indirectly cause calls to `std::is_constant_evaluated` within an invocation of the function to produce a different value. — *end note*]

**9** The `constexpr` and `consteval` specifiers have no effect on the type of a constexpr function. [*Example 5*:

```cpp
constexpr int bar(int x, int y)         // OK
    { return x + y + x*y; }
// ...
int bar(int x, int y)                   // error: redefinition of bar
    { return x * 2 + 3 * y; }
```

— *end example*]

**10** A `constexpr` specifier used in an object declaration declares the object as const. Such an object shall have literal type and shall be initialized. In any `constexpr` variable declaration, the full-expression of the initialization shall be a constant expression ([[expr.const]](expr.const.md)). A `constexpr` variable shall have constant destruction. [*Example 6*:

```cpp
struct pixel {
  int x, y;
};
constexpr pixel ur = { 1294, 1024 };    // OK
constexpr pixel origin;                 // error: initializer missing
```

— *end example*]
