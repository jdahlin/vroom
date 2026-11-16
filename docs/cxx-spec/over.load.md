# 12.2 Overloadable declarations [over.load]

[Contents](README.md) · up: [12 Overloading \[over\]](over.md) · prev: [12.1 Preamble \[over.pre\]](over.pre.md) · next: [12.3 Declaration matching \[over.dcl\]](over.dcl.md)

**1** Not all function declarations can be overloaded. Those that cannot be overloaded are specified here. A program is ill-formed if it contains two such non-overloadable declarations in the same scope. [*Note 1*: This restriction applies to explicit declarations in a scope, and between such declarations and declarations made through a *using-declaration* ([[namespace.udecl]](namespace.udecl.md)). It does not apply to sets of functions fabricated as a result of name lookup (e.g., because of *using-directive*s) or overload resolution (e.g., for operator functions). — *end note*]

**2** Certain function declarations cannot be overloaded:

- Function declarations that differ only in the return type, the exception specification ([[except.spec]](except.spec.md)), or both cannot be overloaded.

- Member function declarations with the same name, the same parameter-type-list ([[dcl.fct]](dcl.fct.md)), and the same trailing *requires-clause* (if any) cannot be overloaded if any of them is a `static` member function declaration ([[class.static]](class.static.md)). Likewise, member function template declarations with the same name, the same parameter-type-list, the same trailing *requires-clause* (if any), and the same *template-head* cannot be overloaded if any of them is a `static` member function template declaration. The types of the implicit object parameters constructed for the member functions for the purpose of overload resolution ([[over.match.funcs]](over.match.funcs.md)) are not considered when comparing parameter-type-lists for enforcement of this rule. In contrast, if there is no `static` member function declaration among a set of member function declarations with the same name, the same parameter-type-list, and the same trailing *requires-clause* (if any), then these member function declarations can be overloaded if they differ in the type of their implicit object parameter. [*Example 1*: The following illustrates this distinction:

  ```cpp
  class X {
    static void f();
    void f();                     // error
    void f() const;               // error
    void f() const volatile;      // error
    void g();
    void g() const;               // OK: no static g
    void g() const volatile;      // OK: no static g
  };
  ```

  — *end example*]

- Member function declarations with the same name, the same parameter-type-list ([[dcl.fct]](dcl.fct.md)), and the same trailing *requires-clause* (if any), as well as member function template declarations with the same name, the same parameter-type-list, the same trailing *requires-clause* (if any), and the same *template-head*, cannot be overloaded if any of them, but not all, have a *ref-qualifier* ([[dcl.fct]](dcl.fct.md)). [*Example 2*:

  ```cpp
  class Y {
    void h() &;
    void h() const &;             // OK
    void h() &&;                  // OK, all declarations have a ref-qualifier
    void i() &;
    void i() const;               // error: prior declaration of i has a ref-qualifier
  };
  ```

  — *end example*]

**3** [*Note 2*: As specified in [[dcl.fct]](dcl.fct.md), function declarations that have equivalent parameter declarations and *requires-clause*s, if any ([[temp.constr.decl]](temp.constr.decl.md)), declare the same function and therefore cannot be overloaded:

- Parameter declarations that differ only in the use of equivalent typedef “types” are equivalent. A `typedef` is not a separate type, but only a synonym for another type ([[dcl.typedef]](dcl.typedef.md)). [*Example 3*:

  ```cpp
  typedef int Int;

  void f(int i);
  void f(Int i);                  // OK: redeclaration of f(int)
  void f(int i) { /* ... */ }
  void f(Int i) { /* ... */ }     // error: redefinition of f(int)
  ```

  — *end example*]

  Enumerations, on the other hand, are distinct types and can be used to distinguish overloaded function declarations. [*Example 4*:

  ```cpp
  enum E { a };

  void f(int i) { /* ... */ }
  void f(E i)   { /* ... */ }
  ```

  — *end example*]

- Parameter declarations that differ only in a pointer `*` versus an array `[]` are equivalent. That is, the array declaration is adjusted to become a pointer declaration ([[dcl.fct]](dcl.fct.md)). Only the second and subsequent array dimensions are significant in parameter types ([[dcl.array]](dcl.array.md)). [*Example 5*:

  ```cpp
  int f(char*);
  int f(char[]);                  // same as f(char*);
  int f(char[7]);                 // same as f(char*);
  int f(char[9]);                 // same as f(char*);

  int g(char(*)[10]);
  int g(char[5][10]);             // same as g(char(*)[10]);
  int g(char[7][10]);             // same as g(char(*)[10]);
  int g(char(*)[20]);             // different from g(char(*)[10]);
  ```

  — *end example*]

- Parameter declarations that differ only in that one is a function type and the other is a pointer to the same function type are equivalent. That is, the function type is adjusted to become a pointer to function type ([[dcl.fct]](dcl.fct.md)). [*Example 6*:

  ```cpp
  void h(int());
  void h(int (*)());              // redeclaration of h(int())
  void h(int x()) { }             // definition of h(int())
  void h(int (*x)()) { }          // error: redefinition of h(int())
  ```

  — *end example*]

- Parameter declarations that differ only in the presence or absence of `const` and/or `volatile` are equivalent. That is, the `const` and `volatile` type-specifiers for each parameter type are ignored when determining which function is being declared, defined, or called. [*Example 7*:

  ```cpp
  typedef const int cInt;

  int f (int);
  int f (const int);              // redeclaration of f(int)
  int f (int) { /* ... */ }       // definition of f(int)
  int f (cInt) { /* ... */ }      // error: redefinition of f(int)
  ```

  — *end example*]

  Only the `const` and `volatile` type-specifiers at the outermost level of the parameter type specification are ignored in this fashion; `const` and `volatile` type-specifiers buried within a parameter type specification are significant and can be used to distinguish overloaded function declarations.[^1] In particular, for any type `T`, “pointer to `T`”, “pointer to `const` `T`”, and “pointer to `volatile` `T`” are considered distinct parameter types, as are “reference to `T`”, “reference to `const` `T`”, and “reference to `volatile` `T`”.

- Two parameter declarations that differ only in their default arguments are equivalent. [*Example 8*: Consider the following:

  ```cpp
  void f (int i, int j);
  void f (int i, int j = 99);     // OK: redeclaration of f(int, int)
  void f (int i = 88, int j);     // OK: redeclaration of f(int, int)
  void f ();                      // OK: overloaded declaration of f

  void prog () {
      f (1, 2);                   // OK: call f(int, int)
      f (1);                      // OK: call f(int, int)
      f ();                       // error: f(int, int) or f()?
  }
  ```

  — *end example*]

— *end note*]

[^1]: When a parameter type includes a function type, such as in the case of a parameter type that is a pointer to function, the `const` and `volatile` type-specifiers at the outermost level of the parameter type specifications for the inner function type are also ignored.
