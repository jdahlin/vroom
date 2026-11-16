# C.5.6 [[class]](class.md): classes [diff.class]

[Contents](README.md) · up: [C.5 C++ and ISO C \[diff.iso\]](diff.iso.md) · prev: [C.5.5 [[dcl.dcl]](dcl.dcl.md): declarations \[diff.dcl\]](diff.dcl.md) · next: [C.5.7 [[cpp]](cpp.md): preprocessing directives \[diff.cpp\]](diff.cpp.md)

**1** **Affected subclause:** [[class.name]](class.name.md) [see also [[dcl.typedef]](dcl.typedef.md)] <br>**Change:** In C++, a class declaration introduces the class name into the scope where it is declared and hides any object, function or other declaration of that name in an enclosing scope. In C, an inner scope declaration of a struct tag name never hides the name of an object or function in an outer scope.

Example:

```cpp
int x[99];
void f() {
  struct x { int a; };
  sizeof(x);  /* size of the array in C */
  /* size of the struct in C++ */
}
```

**Rationale:** This is one of the few incompatibilities between C and C++ that can be attributed to the new C++ name space definition where a name can be declared as a type and as a non-type in a single scope causing the non-type name to hide the type name and requiring that the keywords `class`, `struct`, `union` or `enum` be used to refer to the type name. This new name space definition provides important notational conveniences to C++ programmers and helps making the use of the user-defined types as similar as possible to the use of fundamental types. The advantages of the new name space definition were judged to outweigh by far the incompatibility with C described above. <br>**Effect on original feature:** Change to semantics of well-defined feature. <br>**Difficulty of converting:** Semantic transformation. If the hidden name that needs to be accessed is at global scope, the `::` C++ operator can be used. If the hidden name is at block scope, either the type or the struct tag has to be renamed. <br>**How widely used:** Seldom.

**2** **Affected subclause:** [[class.copy.ctor]](class.copy.ctor.md) <br>**Change:** Copying volatile objects.

The implicitly-declared copy constructor and implicitly-declared copy assignment operator cannot make a copy of a volatile lvalue. For example, the following is valid in ISO C:

```cpp
struct X { int i; };
volatile struct X x1 = {0};
struct X x2 = x1;               // invalid C++
struct X x3;
x3 = x1;                        // also invalid C++
```

**Rationale:** Several alternatives were debated at length. Changing the parameter to `volatile` `const` `X&` would greatly complicate the generation of efficient code for class objects. Discussion of providing two alternative signatures for these implicitly-defined operations raised unanswered concerns about creating ambiguities and complicating the rules that specify the formation of these operators according to the bases and members. <br>**Effect on original feature:** Deletion of semantically well-defined feature. <br>**Difficulty of converting:** Semantic transformation. If volatile semantics are required for the copy, a user-declared constructor or assignment must be provided. If non-volatile semantics are required, an explicit `const_cast` can be used. <br>**How widely used:** Seldom.

**3** **Affected subclause:** [[class.bit]](class.bit.md) <br>**Change:** Bit-fields of type plain `int` are signed. <br>**Rationale:** Leaving the choice of signedness to implementations could lead to inconsistent definitions of template specializations. For consistency, the implementation freedom was eliminated for non-dependent types, too. <br>**Effect on original feature:** The choice is implementation-defined in C, but not so in C++. <br>**Difficulty of converting:** Syntactic transformation. <br>**How widely used:** Seldom.

**4** **Affected subclause:** [[class.nest]](class.nest.md) <br>**Change:** In C++, the name of a nested class is local to its enclosing class. In C the name of the nested class belongs to the same scope as the name of the outermost enclosing class.

Example:

```cpp
struct X {
  struct Y { /* ... */ } y;
};
struct Y yy;                    // valid C, invalid C++
```

**Rationale:** C++ classes have member functions which require that classes establish scopes. The C rule would leave classes as an incomplete scope mechanism which would prevent C++ programmers from maintaining locality within a class. A coherent set of scope rules for C++ based on the C rule would be very complicated and C++ programmers would be unable to predict reliably the meanings of nontrivial examples involving nested or local functions. <br>**Effect on original feature:** Change to semantics of well-defined feature. <br>**Difficulty of converting:** Semantic transformation. To make the struct type name visible in the scope of the enclosing struct, the struct tag could be declared in the scope of the enclosing struct, before the enclosing struct is defined. Example:

```cpp
struct Y;                       // struct Y and struct X are at the same scope
struct X {
  struct Y { /* ... */ } y;
};
```

All the definitions of C struct types enclosed in other struct definitions and accessed outside the scope of the enclosing struct could be exported to the scope of the enclosing struct. Note: this is a consequence of the difference in scope rules, which is documented in [[basic.scope]](basic.scope.md). <br>**How widely used:** Seldom.

**5** **Affected subclause:** [[class.nested.type]](class.nested.type.md) <br>**Change:** In C++, a typedef name may not be redeclared in a class definition after being used in that definition.

Example:

```cpp
typedef int I;
struct S {
  I i;
  int I;            // valid C, invalid C++
};
```

**Rationale:** When classes become complicated, allowing such a redefinition after the type has been used can create confusion for C++ programmers as to what the meaning of `I` really is. <br>**Effect on original feature:** Deletion of semantically well-defined feature. <br>**Difficulty of converting:** Semantic transformation. Either the type or the struct member has to be renamed. <br>**How widely used:** Seldom.
