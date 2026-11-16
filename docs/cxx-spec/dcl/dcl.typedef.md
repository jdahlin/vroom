# 9.2.3 The `typedef` specifier [dcl.typedef]

[Contents](../README.md) · up: [9.2 Specifiers \[dcl.spec\]](dcl.spec.md) · prev: [9.2.2 Function specifiers \[dcl.fct.spec\]](dcl.fct.spec.md) · next: [9.2.4 The `friend` specifier \[dcl.friend\]](dcl.friend.md)

**1** Declarations containing the *decl-specifier* `typedef` declare identifiers that can be used later for naming fundamental ([[basic.fundamental]](../basic/basic.fundamental.md)) or compound ([[basic.compound]](../basic/basic.compound.md)) types. The `typedef` specifier shall not be combined in a *decl-specifier-seq* with any other kind of specifier except a *defining-type-specifier*, and it shall not be used in the *decl-specifier-seq* of a *parameter-declaration* ([[dcl.fct]](dcl.fct.md)) nor in the *decl-specifier-seq* of a *function-definition* ([[dcl.fct.def]](dcl.fct.def.md)). If a `typedef` specifier appears in a declaration without a *declarator*, the program is ill-formed.

```bnf
typedef-name:
    identifier
    simple-template-id
```

A name declared with the `typedef` specifier becomes a *typedef-name*. A *typedef-name* names the type associated with the *identifier* ([[dcl.decl]](dcl.decl.md)) or *simple-template-id* ([[temp.pre]](../temp/temp.pre.md)); a *typedef-name* is thus a synonym for another type. A *typedef-name* does not introduce a new type the way a class declaration ([[class.name]](../class/class.name.md)) or enum declaration ([[dcl.enum]](dcl.enum.md)) does. [*Example 1*: After

```cpp
typedef int MILES, *KLICKSP;
```

the constructions

```cpp
MILES distance;
extern KLICKSP metricp;
```

are all correct declarations; the type of `distance` is `int` and that of `metricp` is “pointer to `int`”. — *end example*]

**2** A *typedef-name* can also be introduced by an *alias-declaration*. The *identifier* following the `using` keyword becomes a *typedef-name* and the optional *attribute-specifier-seq* following the *identifier* appertains to that *typedef-name*. Such a *typedef-name* has the same semantics as if it were introduced by the `typedef` specifier. In particular, it does not define a new type. [*Example 2*:

```cpp
using handler_t = void (*)(int);
extern handler_t ignore;
extern void (*ignore)(int);         // redeclare ignore
using cell = pair<void*, cell*>;    // error
```

— *end example*] The *defining-type-specifier-seq* of the *defining-type-id* shall not define a class or enumeration if the *alias-declaration* is the *declaration* of a *template-declaration*.

**3** In a given non-class scope, a `typedef` specifier can be used to redeclare the name of any type declared in that scope to refer to the type to which it already refers. [*Example 3*:

```cpp
typedef struct s { /* ... */ } s;
typedef int I;
typedef int I;
typedef I I;
```

— *end example*]

**4** In a given class scope, a `typedef` specifier can be used to redeclare any *class-name* declared in that scope that is not also a *typedef-name* to refer to the type to which it already refers. [*Example 4*:

```cpp
struct S {
  typedef struct A { } A;       // OK
  typedef struct B B;           // OK
  typedef A A;                  // error
};
```

— *end example*]

**5** If a `typedef` specifier is used to redeclare in a given scope an entity that can be referenced using an *elaborated-type-specifier*, the entity can continue to be referenced by an *elaborated-type-specifier* or as an enumeration or class name in an enumeration or class definition respectively. [*Example 5*:

```cpp
struct S;
typedef struct S S;
int main() {
  struct S* p;                  // OK
}
struct S { };                   // OK
```

— *end example*]

**6** In a given scope, a `typedef` specifier shall not be used to redeclare the name of any type declared in that scope to refer to a different type. [*Example 6*:

```cpp
class complex { /* ... */ };
typedef int complex;            // error: redefinition
```

— *end example*]

**7** Similarly, in a given scope, a class or enumeration shall not be declared with the same name as a *typedef-name* that is declared in that scope and refers to a type other than the class or enumeration itself. [*Example 7*:

```cpp
typedef int complex;
class complex { /* ... */ };    // error: redefinition
```

— *end example*]

**8** A *simple-template-id* is only a *typedef-name* if its *template-name* names an alias template or a template *template-parameter*. [*Note 1*: A *simple-template-id* that names a class template specialization is a *class-name* ([[class.name]](../class/class.name.md)). If a *typedef-name* is used to identify the subject of an *elaborated-type-specifier* ([[dcl.type.elab]](dcl.type.elab.md)), a class definition ([[class]](../class/class.md)), a constructor declaration ([[class.ctor]](../class/class.ctor.md)), or a destructor declaration ([[class.dtor]](../class/class.dtor.md)), the program is ill-formed. — *end note*] [*Example 8*:

```cpp
struct S {
  S();
  ~S();
};

typedef struct S T;

S a = T();                      // OK
struct T * p;                   // error
```

— *end example*]

**9** If the typedef declaration defines an unnamed class or enumeration, the first *typedef-name* declared by the declaration to be that type is used to denote the type for linkage purposes only ([[basic.link]](../basic/basic.link.md)). [*Note 2*: A typedef declaration involving a *lambda-expression* does not itself define the associated closure type, and so the closure type is not given a name for linkage purposes. — *end note*] [*Example 9*:

```cpp
typedef struct { } *ps, S;      // S is the class name for linkage purposes
typedef decltype([]{}) C;       // the closure type has no name for linkage purposes
```

— *end example*]

**10** An unnamed class with a typedef name for linkage purposes shall not

- declare any members other than non-static data members, member enumerations, or member classes,
- have any base classes or default member initializers, or
- contain a *lambda-expression*,

and all member classes shall also satisfy these requirements (recursively). [*Example 10*:

```cpp
typedef struct {
  int f() {}
} X;                            // error: struct with typedef name for linkage has member functions
```

— *end example*]
