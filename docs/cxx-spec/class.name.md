# 11.3 Class names [class.name]

[Contents](README.md) · up: [11 Classes \[class\]](class.md) · prev: [11.2 Properties of classes \[class.prop\]](class.prop.md) · next: [11.4 Class members \[class.mem\]](class.mem.md)

**1** A class definition introduces a new type. [*Example 1*:

```cpp
struct X { int a; };
struct Y { int a; };
X a1;
Y a2;
int a3;
```

declares three variables of three different types. This implies that

```cpp
a1 = a2;                        // error: Y assigned to X
a1 = a3;                        // error: int assigned to X
```

are type mismatches, and that

```cpp
int f(X);
int f(Y);
```

declare an overloaded ([[over]](over.md)) function `f()` and not simply a single function `f()` twice. For the same reason,

```cpp
struct S { int a; };
struct S { int a; };            // error: double definition
```

is ill-formed because it defines `S` twice. — *end example*]

**2** A class declaration introduces the class name into the scope where it is declared and hides any class, variable, function, or other declaration of that name in an enclosing scope ([[basic.scope]](basic.scope.md)). If a class name is declared in a scope where a variable, function, or enumerator of the same name is also declared, then when both declarations are in scope, the class can be referred to only using an *elaborated-type-specifier* ([[basic.lookup.elab]](basic.lookup.elab.md)). [*Example 2*:

```cpp
struct stat {
  // ...
};

stat gstat;                     // use plain stat to define variable

int stat(struct stat*);         // redeclare stat as function

void f() {
  struct stat* ps;              // struct prefix needed to name struct stat
  stat(ps);                     // call stat()
}
```

— *end example*] A *declaration* consisting solely of *class-key* *identifier*`;` is either a redeclaration of the name in the current scope or a forward declaration of the identifier as a class name. It introduces the class name into the current scope. [*Example 3*:

```cpp
struct s { int a; };

void g() {
  struct s;                     // hide global struct s with a block-scope declaration
  s* p;                         // refer to local struct s
  struct s { char* p; };        // define local struct s
  struct s;                     // redeclaration, has no effect
}
```

— *end example*] [*Note 1*: Such declarations allow definition of classes that refer to each other. [*Example 4*:

```cpp
class Vector;

class Matrix {
  // ...
  friend Vector operator*(const Matrix&, const Vector&);
};

class Vector {
  // ...
  friend Vector operator*(const Matrix&, const Vector&);
};
```

Declaration of friends is described in [[class.friend]](class.friend.md), operator functions in [[over.oper]](over.oper.md). — *end example*] — *end note*]

**3** [*Note 2*: An *elaborated-type-specifier* ([[dcl.type.elab]](dcl.type.elab.md)) can also be used as a *type-specifier* as part of a declaration. It differs from a class declaration in that if a class of the elaborated name is in scope the elaborated name will refer to it. — *end note*] [*Example 5*:

```cpp
struct s { int a; };

void g(int s) {
  struct s* p = new struct s;   // global s
  p->a = s;                     // parameter s
}
```

— *end example*]

**4** [*Note 3*: The declaration of a class name takes effect immediately after the *identifier* is seen in the class definition or *elaborated-type-specifier*. For example,

```cpp
class A * A;
```

first specifies `A` to be the name of a class and then redefines it as the name of a pointer to an object of that class. This means that the elaborated form `class` `A` must be used to refer to the class. Such artistry with names can be confusing and is best avoided. — *end note*]

**5** A *simple-template-id* is only a *class-name* if its *template-name* names a class template.
