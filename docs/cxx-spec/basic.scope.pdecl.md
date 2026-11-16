# 6.4.2 Point of declaration [basic.scope.pdecl]

[Contents](README.md) · up: [6.4 Scope \[basic.scope\]](basic.scope.md) · prev: [6.4.1 Declarative regions and scopes \[basic.scope.declarative\]](basic.scope.declarative.md) · next: [6.4.3 Block scope \[basic.scope.block\]](basic.scope.block.md)

**1** The *point of declaration* for a name is immediately after its complete declarator ([[dcl.decl]](dcl.decl.md)) and before its *initializer* (if any), except as noted below. [*Example 1*:

```cpp
unsigned char x = 12;
{ unsigned char x = x; }
```

Here, the initialization of the second `x` has undefined behavior, because the initializer accesses the second `x` outside its lifetime ([[basic.life]](basic.life.md)). — *end example*]

**2** [*Note 1*: A name from an outer scope remains visible up to the point of declaration of the name that hides it. [*Example 2*:

```cpp
const int  i = 2;
{ int  i[i]; }
```

declares a block-scope array of two integers. — *end example*] — *end note*]

**3** The point of declaration for a class or class template first declared by a *class-specifier* is immediately after the *identifier* or *simple-template-id* (if any) in its *class-head* ([[class.pre]](class.pre.md)). The point of declaration for an enumeration is immediately after the *identifier* (if any) in either its *enum-specifier* ([[dcl.enum]](dcl.enum.md)) or its first *opaque-enum-declaration* ([[dcl.enum]](dcl.enum.md)), whichever comes first. The point of declaration of an alias or alias template immediately follows the *defining-type-id* to which the alias refers.

**4** The point of declaration of a *using-declarator* that does not name a constructor is immediately after the *using-declarator* ([[namespace.udecl]](namespace.udecl.md)).

**5** The point of declaration for an enumerator is immediately after its *enumerator-definition*. [*Example 3*:

```cpp
const int x = 12;
{ enum { x = x }; }
```

Here, the enumerator `x` is initialized with the value of the constant `x`, namely 12. — *end example*]

**6** After the point of declaration of a class member, the member name can be looked up in the scope of its class. [*Note 2*: This is true even if the class is an incomplete class. For example,

```cpp
struct X {
  enum E { z = 16 };
  int b[X::z];      // OK
};
```

— *end note*]

**7** The point of declaration of a class first declared in an *elaborated-type-specifier* is as follows:

- for a declaration of the form

  ```bnf
  class-key attribute-specifier-seqₒₚₜ identifier ';'
  ```

  the *identifier* is declared to be a *class-name* in the scope that contains the declaration, otherwise

- for an *elaborated-type-specifier* of the form

  ```bnf
  class-key identifier
  ```

  if the *elaborated-type-specifier* is used in the *decl-specifier-seq* or *parameter-declaration-clause* of a function defined in namespace scope, the *identifier* is declared as a *class-name* in the namespace that contains the declaration; otherwise, except as a friend declaration, the *identifier* is declared in the smallest namespace or block scope that contains the declaration. [*Note 3*: These rules also apply within templates. — *end note*] [*Note 4*: Other forms of *elaborated-type-specifier* do not declare a new name, and therefore must refer to an existing *type-name*. See [[basic.lookup.elab]](basic.lookup.elab.md) and [[dcl.type.elab]](dcl.type.elab.md). — *end note*]

**8** The point of declaration for an injected-class-name ([[class.pre]](class.pre.md)) is immediately following the opening brace of the class definition.

**9** The point of declaration for a function-local predefined variable ([[dcl.fct.def.general]](dcl.fct.def.general.md)) is immediately before the *function-body* of a function definition.

**10** The point of declaration of a structured binding ([[dcl.struct.bind]](dcl.struct.bind.md)) is immediately after the *identifier-list* of the structured binding declaration.

**11** The point of declaration for the variable or the structured bindings declared in the *for-range-declaration* of a range-based `for` statement ([[stmt.ranged]](stmt.ranged.md)) is immediately after the *for-range-initializer*.

**12** The point of declaration for a template parameter is immediately after its complete *template-parameter*. [*Example 4*:

```cpp
typedef unsigned char T;
template<class T
  = T               // lookup finds the typedef name of unsigned char
  , T               // lookup finds the template parameter
    N = 0> struct A { };
```

— *end example*]

**13** [*Note 5*: Friend declarations refer to functions or classes that are members of the nearest enclosing namespace, but they do not introduce new names into that namespace ([[namespace.memdef]](namespace.memdef.md)). Function declarations at block scope and variable declarations with the `extern` specifier at block scope refer to declarations that are members of an enclosing namespace, but they do not introduce new names into that scope. — *end note*]

**14** [*Note 6*: For point of instantiation of a template, see [[temp.point]](temp.point.md). — *end note*]
