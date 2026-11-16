# C.5.5 [[dcl.dcl]](../dcl/dcl.dcl.md): declarations [diff.dcl]

[Contents](../README.md) · up: [C.5 C++ and ISO C \[diff.iso\]](diff.iso.md) · prev: [C.5.4 [[stmt.stmt]](../stmt/stmt.stmt.md): statements \[diff.stat\]](diff.stat.md) · next: [C.5.6 [[class]](../class/class.md): classes \[diff.class\]](diff.class.md)

**1** **Affected subclause:** [[dcl.stc]](../dcl/dcl.stc.md) <br>**Change:** In C++, the `static` or `extern` specifiers can only be applied to names of objects or functions.<br> Using these specifiers with type declarations is illegal in C++. In C, these specifiers are ignored when used on type declarations.

Example:

```cpp
static struct S {               // valid C, invalid in C++
  int i;
};
```

**Rationale:** Storage class specifiers don’t have any meaning when associated with a type. In C++, class members can be declared with the `static` storage class specifier. Allowing storage class specifiers on type declarations could render the code confusing for users. <br>**Effect on original feature:** Deletion of semantically well-defined feature. <br>**Difficulty of converting:** Syntactic transformation. <br>**How widely used:** Seldom.

**2** **Affected subclause:** [[dcl.stc]](../dcl/dcl.stc.md) <br>**Change:** In C++, `register` is not a storage class specifier. <br>**Rationale:** The storage class specifier had no effect in C++. <br>**Effect on original feature:** Deletion of semantically well-defined feature. <br>**Difficulty of converting:** Syntactic transformation. <br>**How widely used:** Common.

**3** **Affected subclause:** [[dcl.typedef]](../dcl/dcl.typedef.md) <br>**Change:** A C++ typedef name must be different from any class type name declared in the same scope (except if the typedef is a synonym of the class name with the same name). In C, a typedef name and a struct tag name declared in the same scope can have the same name (because they have different name spaces).

Example:

```cpp
typedef struct name1 { /* ... */ } name1;         // valid C and C++
struct name { /* ... */ };
typedef int name;               // valid C, invalid C++
```

**Rationale:** For ease of use, C++ doesn’t require that a type name be prefixed with the keywords `class`, `struct` or `union` when used in object declarations or type casts.

Example:

```cpp
class name { /* ... */ };
name i;                         // i has type class name
```

**Effect on original feature:** Deletion of semantically well-defined feature. <br>**Difficulty of converting:** Semantic transformation. One of the 2 types has to be renamed. <br>**How widely used:** Seldom.

**4** **Affected subclause:** [[dcl.type]](../dcl/dcl.type.md) [see also [[basic.link]](../basic/basic.link.md)] <br>**Change:** Const objects must be initialized in C++ but can be left uninitialized in C. <br>**Rationale:** A const object cannot be assigned to so it must be initialized to hold a useful value. <br>**Effect on original feature:** Deletion of semantically well-defined feature. <br>**Difficulty of converting:** Semantic transformation. <br>**How widely used:** Seldom.

**5** **Affected subclause:** [[dcl.type]](../dcl/dcl.type.md) <br>**Change:** Banning implicit `int`.

In C++ a *decl-specifier-seq* must contain a *type-specifier*, unless it is followed by a declarator for a constructor, a destructor, or a conversion function. In the following example, the left-hand column presents valid C; the right-hand column presents equivalent C++:

```cpp
void f(const parm);            void f(const int parm);
const n = 3;                   const int n = 3;
main()                         int main()
    /* ... */                      /* ... */
```

**Rationale:** In C++, implicit int creates several opportunities for ambiguity between expressions involving function-like casts and declarations. Explicit declaration is increasingly considered to be proper style. Liaison with WG14 (C) indicated support for (at least) deprecating implicit int in the next revision of C. <br>**Effect on original feature:** Deletion of semantically well-defined feature. <br>**Difficulty of converting:** Syntactic transformation. Could be automated. <br>**How widely used:** Common.

**6** **Affected subclause:** [[dcl.spec.auto]](../dcl/dcl.spec.auto.md) <br>**Change:** The keyword `auto` cannot be used as a storage class specifier.

```cpp
void f() {
  auto int x;       // valid C, invalid C++
}
```

**Rationale:** Allowing the use of `auto` to deduce the type of a variable from its initializer results in undesired interpretations of `auto` as a storage class specifier in certain contexts. <br>**Effect on original feature:** Deletion of semantically well-defined feature. <br>**Difficulty of converting:** Syntactic transformation. <br>**How widely used:** Rare.

**7** **Affected subclause:** [[dcl.fct]](../dcl/dcl.fct.md) <br>**Change:** In C++, a function declared with an empty parameter list takes no arguments. In C, an empty parameter list means that the number and type of the function arguments are unknown.

Example:

```cpp
int f();            // means int f(void) in C++
                    // int f( unknown ) in C
```

**Rationale:** This is to avoid erroneous function calls (i.e., function calls with the wrong number or type of arguments). <br>**Effect on original feature:** Change to semantics of well-defined feature. This feature was marked as “obsolescent” in C. <br>**Difficulty of converting:** Syntactic transformation. The function declarations using C incomplete declaration style must be completed to become full prototype declarations. A program may need to be updated further if different calls to the same (non-prototype) function have different numbers of arguments or if the type of corresponding arguments differed. <br>**How widely used:** Common.

**8** **Affected subclause:** [[dcl.fct]](../dcl/dcl.fct.md) [see [[expr.sizeof]](../expr/expr.sizeof.md)] <br>**Change:** In C++, types may not be defined in return or parameter types. In C, these type definitions are allowed.

Example:

```cpp
void f( struct S { int a; } arg ) {}    // valid C, invalid C++
enum E { A, B, C } f() {}               // valid C, invalid C++
```

**Rationale:** When comparing types in different translation units, C++ relies on name equivalence when C relies on structural equivalence. Regarding parameter types: since the type defined in a parameter list would be in the scope of the function, the only legal calls in C++ would be from within the function itself. <br>**Effect on original feature:** Deletion of semantically well-defined feature. <br>**Difficulty of converting:** Semantic transformation. The type definitions must be moved to file scope, or in header files. <br>**How widely used:** Seldom. This style of type definition is seen as poor coding style.

**9** **Affected subclause:** [[dcl.fct.def]](../dcl/dcl.fct.def.md) <br>**Change:** In C++, the syntax for function definition excludes the “old-style” C function. In C, “old-style” syntax is allowed, but deprecated as “obsolescent”. <br>**Rationale:** Prototypes are essential to type safety. <br>**Effect on original feature:** Deletion of semantically well-defined feature. <br>**Difficulty of converting:** Syntactic transformation. <br>**How widely used:** Common in old programs, but already known to be obsolescent.

**10** **Affected subclause:** [[dcl.init.aggr]](../dcl/dcl.init.aggr.md) <br>**Change:** In C++, designated initialization support is restricted compared to the corresponding functionality in C. In C++, designators for non-static data members must be specified in declaration order, designators for array elements and nested designators are not supported, and designated and non-designated initializers cannot be mixed in the same initializer list.

Example:

```cpp
struct A { int x, y; };
struct B { struct A a; };
struct A a = {.y = 1, .x = 2};  // valid C, invalid C++
int arr[3] = {[1] = 5};         // valid C, invalid C++
struct B b = {.a.x = 0};        // valid C, invalid C++
struct A c = {.x = 1, 2};       // valid C, invalid C++
```

**Rationale:** In C++, members are destroyed in reverse construction order and the elements of an initializer list are evaluated in lexical order, so field initializers must be specified in order. Array designators conflict with *lambda-expression* syntax. Nested designators are seldom used. <br>**Effect on original feature:** Deletion of feature that is incompatible with C++. <br>**Difficulty of converting:** Syntactic transformation. <br>**How widely used:** Out-of-order initializers are common. The other features are seldom used.

**11** **Affected subclause:** [[dcl.init.string]](../dcl/dcl.init.string.md) <br>**Change:** In C++, when initializing an array of character with a string, the number of characters in the string (including the terminating `'\0'`) must not exceed the number of elements in the array. In C, an array can be initialized with a string even if the array is not large enough to contain the string-terminating `'\0'`.

Example:

```cpp
char array[4] = "abcd";         // valid C, invalid C++
```

**Rationale:** When these non-terminated arrays are manipulated by standard string functions, there is potential for major catastrophe. <br>**Effect on original feature:** Deletion of semantically well-defined feature. <br>**Difficulty of converting:** Semantic transformation. The arrays must be declared one element bigger to contain the string terminating `'\0'`. <br>**How widely used:** Seldom. This style of array initialization is seen as poor coding style.

**12** **Affected subclause:** [[dcl.enum]](../dcl/dcl.enum.md) <br>**Change:** C++ objects of enumeration type can only be assigned values of the same enumeration type. In C, objects of enumeration type can be assigned values of any integral type.

Example:

```cpp
enum color { red, blue, green };
enum color c = 1;               // valid C, invalid C++
```

**Rationale:** The type-safe nature of C++. <br>**Effect on original feature:** Deletion of semantically well-defined feature. <br>**Difficulty of converting:** Syntactic transformation. (The type error produced by the assignment can be automatically corrected by applying an explicit cast.) <br>**How widely used:** Common.

**13** **Affected subclause:** [[dcl.enum]](../dcl/dcl.enum.md) <br>**Change:** In C++, the type of an enumerator is its enumeration. In C, the type of an enumerator is `int`.

Example:

```cpp
enum e { A };
sizeof(A) == sizeof(int)        // in C
sizeof(A) == sizeof(e)          // in C++
/* and sizeof(int) is not necessarily equal to sizeof(e) */
```

**Rationale:** In C++, an enumeration is a distinct type. <br>**Effect on original feature:** Change to semantics of well-defined feature. <br>**Difficulty of converting:** Semantic transformation. <br>**How widely used:** Seldom. The only time this affects existing C code is when the size of an enumerator is taken. Taking the size of an enumerator is not a common C coding practice.
