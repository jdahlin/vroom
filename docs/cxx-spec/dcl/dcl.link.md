# 9.11 Linkage specifications [dcl.link]

[Contents](../README.md) · up: [9 Declarations \[dcl.dcl\]](dcl.dcl.md) · prev: [9.10 The `asm` declaration \[dcl.asm\]](dcl.asm.md) · next: [9.12 Attributes \[dcl.attr\]](dcl.attr.md)

**1** All function types, function names with external linkage, and variable names with external linkage have a *language linkage*. [*Note 1*: Some of the properties associated with an entity with language linkage are specific to each implementation and are not described here. For example, a particular language linkage may be associated with a particular form of representing names of objects and functions with external linkage, or with a particular calling convention, etc. — *end note*] The default language linkage of all function types, function names, and variable names is C++ language linkage. Two function types with different language linkages are distinct types even if they are otherwise identical.

**2** Linkage ([[basic.link]](../basic/basic.link.md)) between C++ and non-C++ code fragments can be achieved using a *linkage-specification*:

```bnf
linkage-specification:
    'extern' string-literal '{' declaration-seqₒₚₜ '}'
    'extern' string-literal declaration
```

The *string-literal* indicates the required language linkage. This document specifies the semantics for the *string-literal*s `"C"` and `"C++"`. Use of a *string-literal* other than `"C"` or `"C++"` is conditionally-supported, with implementation-defined semantics. [*Note 2*: Therefore, a linkage-specification with a *string-literal* that is unknown to the implementation requires a diagnostic. — *end note*] [*Note 3*: It is recommended that the spelling of the *string-literal* be taken from the document defining that language. For example, `Ada` (not `ADA`) and `Fortran` or `FORTRAN`, depending on the vintage. — *end note*]

**3** Every implementation shall provide for linkage to functions written in the C programming language, `"C"`, and linkage to C++ functions, `"C++"`. [*Example 1*:

```cpp
complex sqrt(complex);          // C++ linkage by default
extern "C" {
  double sqrt(double);          // C linkage
}
```

— *end example*]

**4** A *module-import-declaration* shall not be directly contained in a *linkage-specification*. A *module-import-declaration* appearing in a linkage specification with other than C++ language linkage is conditionally-supported with implementation-defined semantics.

**5** Linkage specifications nest. When linkage specifications nest, the innermost one determines the language linkage. A linkage specification does not establish a scope. A *linkage-specification* shall occur only in namespace scope ([[basic.scope]](../basic/basic.scope.md)). In a *linkage-specification*, the specified language linkage applies to the function types of all function declarators, function names with external linkage, and variable names with external linkage declared within the *linkage-specification*. [*Example 2*:

```cpp
extern "C"                      // the name f1 and its function type have C language linkage;
  void f1(void(*pf)(int));      // pf is a pointer to a C function

extern "C" typedef void FUNC();
FUNC f2;                        // the name f2 has C++ language linkage and the
                                // function's type has C language linkage

extern "C" FUNC f3;             // the name of function f3 and the function's type have C language linkage

void (*pf2)(FUNC*);             // the name of the variable pf2 has C++ linkage and the type
                                // of pf2 is ``pointer to C++ function that takes one parameter of type
                                // pointer to C function''
extern "C" {
  static void f4();             // the name of the function f4 has internal linkage (not C language linkage)
                                // and the function's type has C language linkage.
}

extern "C" void f5() {
  extern void f4();             // OK: Name linkage (internal) and function type linkage (C language linkage)
                                // obtained from previous declaration.
}

extern void f4();               // OK: Name linkage (internal) and function type linkage (C language linkage)
                                // obtained from previous declaration.

void f6() {
  extern void f4();             // OK: Name linkage (internal) and function type linkage (C language linkage)
                                // obtained from previous declaration.
}
```

— *end example*] A C language linkage is ignored in determining the language linkage of the names of class members and the function type of class member functions. [*Example 3*:

```cpp
extern "C" typedef void FUNC_c();

class C {
  void mf1(FUNC_c*);            // the name of the function mf1 and the member function's type have
                                // C++ language linkage; the parameter has type ``pointer to C function''

  FUNC_c mf2;                   // the name of the function mf2 and the member function's type have
                                // C++ language linkage

  static FUNC_c* q;             // the name of the data member q has C++ language linkage and
                                // the data member's type is ``pointer to C function''
};

extern "C" {
  class X {
    void mf();                  // the name of the function mf and the member function's type have
                                // C++ language linkage
    void mf2(void(*)());        // the name of the function mf2 has C++ language linkage;
                                // the parameter has type ``pointer to C function''
  };
}
```

— *end example*]

**6** If two declarations declare functions with the same name and parameter-type-list ([[dcl.fct]](dcl.fct.md)) to be members of the same namespace or declare objects with the same name to be members of the same namespace and the declarations give the names different language linkages, the program is ill-formed; no diagnostic is required if the declarations appear in different translation units. Except for functions with C++ linkage, a function declaration without a linkage specification shall not precede the first linkage specification for that function. A function can be declared without a linkage specification after an explicit linkage specification has been seen; the linkage explicitly specified in the earlier declaration is not affected by such a function declaration.

**7** At most one function with a particular name can have C language linkage. Two declarations for a function with C language linkage with the same function name (ignoring the namespace names that qualify it) that appear in different namespace scopes refer to the same function. Two declarations for a variable with C language linkage with the same name (ignoring the namespace names that qualify it) that appear in different namespace scopes refer to the same variable. An entity with C language linkage shall not be declared with the same name as a variable in global scope, unless both declarations denote the same entity; no diagnostic is required if the declarations appear in different translation units. A variable with C language linkage shall not be declared with the same name as a function with C language linkage (ignoring the namespace names that qualify the respective names); no diagnostic is required if the declarations appear in different translation units. [*Note 4*: Only one definition for an entity with a given name with C language linkage may appear in the program (see [[basic.def.odr]](../basic/basic.def.odr.md)); this implies that such an entity must not be defined in more than one namespace scope. — *end note*] [*Example 4*:

```cpp
int x;
namespace A {
  extern "C" int f();
  extern "C" int g() { return 1; }
  extern "C" int h();
  extern "C" int x();               // error: same name as global-space object x
}

namespace B {
  extern "C" int f();               // A::f and B::f refer to the same function
  extern "C" int g() { return 1; }  // error: the function g with C language linkage has two definitions
}

int A::f() { return 98; }           // definition for the function f with C language linkage
extern "C" int h() { return 97; }   // definition for the function h with C language linkage
                                    // A::h and ::h refer to the same function
```

— *end example*]

**8** A declaration directly contained in a *linkage-specification* is treated as if it contains the `extern` specifier ([[dcl.stc]](dcl.stc.md)) for the purpose of determining the linkage of the declared name and whether it is a definition. Such a declaration shall not specify a storage class. [*Example 5*:

```cpp
extern "C" double f();
static double f();                  // error
extern "C" int i;                   // declaration
extern "C" {
  int i;                            // definition
}
extern "C" static void g();         // error
```

— *end example*]

**9** [*Note 5*: Because the language linkage is part of a function type, when indirecting through a pointer to C function, the function to which the resulting lvalue refers is considered a C function. — *end note*]

**10** Linkage from C++ to objects defined in other languages and to objects defined in C++ from other languages is implementation-defined and language-dependent. Only where the object layout strategies of two language implementations are similar enough can such linkage be achieved.
