# 11.10.2 Initializing bases and members [class.base.init]

[Contents](../README.md) · up: [11.10 Initialization \[class.init\]](class.init.md) · prev: [11.10.1 Explicit initialization \[class.expl.init\]](class.expl.init.md) · next: [11.10.3 Initialization by inherited constructor \[class.inhctor.init\]](class.inhctor.init.md)

**1** In the definition of a constructor for a class, initializers for direct and virtual base class subobjects and non-static data members can be specified by a *ctor-initializer*, which has the form

```bnf
ctor-initializer:
    ':' mem-initializer-list
```

```bnf
mem-initializer-list:
    mem-initializer '...'ₒₚₜ
    mem-initializer-list ',' mem-initializer '...'ₒₚₜ
```

```bnf
mem-initializer:
    mem-initializer-id '(' expression-listₒₚₜ ')'
    mem-initializer-id braced-init-list
```

```bnf
mem-initializer-id:
    class-or-decltype
    identifier
```

**2** In a *mem-initializer-id* an initial unqualified *identifier* is looked up in the scope of the constructor’s class and, if not found in that scope, it is looked up in the scope containing the constructor’s definition. [*Note 1*: If the constructor’s class contains a member with the same name as a direct or virtual base class of the class, a *mem-initializer-id* naming the member or base class and composed of a single identifier refers to the class member. A *mem-initializer-id* for the hidden base class may be specified using a qualified name. — *end note*] Unless the *mem-initializer-id* names the constructor’s class, a non-static data member of the constructor’s class, or a direct or virtual base of that class, the *mem-initializer* is ill-formed.

**3** A *mem-initializer-list* can initialize a base class using any *class-or-decltype* that denotes that base class type. [*Example 1*:

```cpp
struct A { A(); };
typedef A global_A;
struct B { };
struct C: public A, public B { C(); };
C::C(): global_A() { }          // mem-initializer for base A
```

— *end example*]

**4** If a *mem-initializer-id* is ambiguous because it designates both a direct non-virtual base class and an inherited virtual base class, the *mem-initializer* is ill-formed. [*Example 2*:

```cpp
struct A { A(); };
struct B: public virtual A { };
struct C: public A, public B { C(); };
C::C(): A() { }                 // error: which A?
```

— *end example*]

**5** A *ctor-initializer* may initialize a variant member of the constructor’s class. If a *ctor-initializer* specifies more than one *mem-initializer* for the same member or for the same base class, the *ctor-initializer* is ill-formed.

**6** A *mem-initializer-list* can delegate to another constructor of the constructor’s class using any *class-or-decltype* that denotes the constructor’s class itself. If a *mem-initializer-id* designates the constructor’s class, it shall be the only *mem-initializer*; the constructor is a *delegating constructor*, and the constructor selected by the *mem-initializer* is the *target constructor*. The target constructor is selected by overload resolution. Once the target constructor returns, the body of the delegating constructor is executed. If a constructor delegates to itself directly or indirectly, the program is ill-formed, no diagnostic required. [*Example 3*:

```cpp
struct C {
  C( int ) { }                  // #1: non-delegating constructor
  C(): C(42) { }                // #2: delegates to #1
  C( char c ) : C(42.0) { }     // #3: ill-formed due to recursion with #4
  C( double d ) : C('a') { }    // #4: ill-formed due to recursion with #3
};
```

— *end example*]

**7** The *expression-list* or *braced-init-list* in a *mem-initializer* is used to initialize the designated subobject (or, in the case of a delegating constructor, the complete class object) according to the initialization rules of [[dcl.init]](../dcl/dcl.init.md) for direct-initialization. [*Example 4*:

```cpp
struct B1 { B1(int); /* ... */ };
struct B2 { B2(int); /* ... */ };
struct D : B1, B2 {
  D(int);
  B1 b;
  const int c;
};

D::D(int a) : B2(a+1), B1(a+2), c(a+3), b(a+4) { /* ... */ }
D d(10);
```

— *end example*] [*Note 2*: The initialization performed by each *mem-initializer* constitutes a full-expression ([[intro.execution]](../basic/intro.execution.md)). Any expression in a *mem-initializer* is evaluated as part of the full-expression that performs the initialization. — *end note*] A *mem-initializer* where the *mem-initializer-id* denotes a virtual base class is ignored during execution of a constructor of any class that is not the most derived class.

**8** A temporary expression bound to a reference member in a *mem-initializer* is ill-formed. [*Example 5*:

```cpp
struct A {
  A() : v(42) { }   // error
  const int& v;
};
```

— *end example*]

**9** In a non-delegating constructor, if a given potentially constructed subobject is not designated by a *mem-initializer-id* (including the case where there is no *mem-initializer-list* because the constructor has no *ctor-initializer*), then

- if the entity is a non-static data member that has a default member initializer ([[class.mem]](class.mem.md)) and either

  - the constructor’s class is a union ([[class.union]](class.union.md)), and no other variant member of that union is designated by a *mem-initializer-id* or
  - the constructor’s class is not a union, and, if the entity is a member of an anonymous union, no other member of that union is designated by a *mem-initializer-id*,

  the entity is initialized from its default member initializer as specified in [[dcl.init]](../dcl/dcl.init.md);

- otherwise, if the entity is an anonymous union or a variant member ([[class.union.anon]](class.union.anon.md)), no initialization is performed;

- otherwise, the entity is default-initialized ([[dcl.init]](../dcl/dcl.init.md)).

[*Note 3*: An abstract class ([[class.abstract]](class.abstract.md)) is never a most derived class, thus its constructors never initialize virtual base classes, therefore the corresponding *mem-initializer*s may be omitted. — *end note*] An attempt to initialize more than one non-static data member of a union renders the program ill-formed. [*Note 4*: After the call to a constructor for class `X` for an object with automatic or dynamic storage duration has completed, if the constructor was not invoked as part of value-initialization and a member of `X` is neither initialized nor given a value during execution of the *compound-statement* of the body of the constructor, the member has an indeterminate value. — *end note*] [*Example 6*:

```cpp
struct A {
  A();
};

struct B {
  B(int);
};

struct C {
  C() { }               // initializes members as follows:
  A a;                  // OK: calls A::A()
  const B b;            // error: B has no default constructor
  int i;                // OK: i has indeterminate value
  int j = 5;            // OK: j has the value 5
};
```

— *end example*]

**10** If a given non-static data member has both a default member initializer and a *mem-initializer*, the initialization specified by the *mem-initializer* is performed, and the non-static data member’s default member initializer is ignored. [*Example 7*: Given

```cpp
struct A {
  int i = /* some integer expression with side effects */ ;
  A(int arg) : i(arg) { }
  // ...
};
```

the `A(int)` constructor will simply initialize `i` to the value of `arg`, and the side effects in `i`’s default member initializer will not take place. — *end example*]

**11** A temporary expression bound to a reference member from a default member initializer is ill-formed. [*Example 8*:

```cpp
struct A {
  A() = default;        // OK
  A(int v) : v(v) { }   // OK
  const int& v = 42;    // OK
};
A a1;                   // error: ill-formed binding of temporary to reference
A a2(1);                // OK, unfortunately
```

— *end example*]

**12** In a non-delegating constructor, the destructor for each potentially constructed subobject of class type is potentially invoked ([[class.dtor]](class.dtor.md)). [*Note 5*: This provision ensures that destructors can be called for fully-constructed subobjects in case an exception is thrown ([[except.ctor]](../except/except.ctor.md)). — *end note*]

**13** In a non-delegating constructor, initialization proceeds in the following order:

- First, and only for the constructor of the most derived class ([[intro.object]](../basic/intro.object.md)), virtual base classes are initialized in the order they appear on a depth-first left-to-right traversal of the directed acyclic graph of base classes, where “left-to-right” is the order of appearance of the base classes in the derived class *base-specifier-list*.
- Then, direct base classes are initialized in declaration order as they appear in the *base-specifier-list* (regardless of the order of the *mem-initializer*s).
- Then, non-static data members are initialized in the order they were declared in the class definition (again regardless of the order of the *mem-initializer*s).
- Finally, the *compound-statement* of the constructor body is executed.

[*Note 6*: The declaration order is mandated to ensure that base and member subobjects are destroyed in the reverse order of initialization. — *end note*]

**14** [*Example 9*:

```cpp
struct V {
  V();
  V(int);
};

struct A : virtual V {
  A();
  A(int);
};

struct B : virtual V {
  B();
  B(int);
};

struct C : A, B, virtual V {
  C();
  C(int);
};

A::A(int i) : V(i) { /* ... */ }
B::B(int i) { /* ... */ }
C::C(int i) { /* ... */ }

V v(1);             // use V(int)
A a(2);             // use V(int)
B b(3);             // use V()
C c(4);             // use V()
```

— *end example*]

**15** Names in the *expression-list* or *braced-init-list* of a *mem-initializer* are evaluated in the scope of the constructor for which the *mem-initializer* is specified. [*Example 10*:

```cpp
class X {
  int a;
  int b;
  int i;
  int j;
public:
  const int& r;
  X(int i): r(a), b(i), i(i), j(this->i) { }
};
```

initializes `X::r` to refer to `X::a`, initializes `X::b` with the value of the constructor parameter `i`, initializes `X::i` with the value of the constructor parameter `i`, and initializes `X::j` with the value of `X::i`; this takes place each time an object of class `X` is created. — *end example*] [*Note 7*: Because the *mem-initializer* are evaluated in the scope of the constructor, the `this` pointer can be used in the *expression-list* of a *mem-initializer* to refer to the object being initialized. — *end note*]

**16** Member functions (including virtual member functions, [[class.virtual]](class.virtual.md)) can be called for an object under construction. Similarly, an object under construction can be the operand of the `typeid` operator ([[expr.typeid]](../expr/expr.typeid.md)) or of a `dynamic_cast` ([[expr.dynamic.cast]](../expr/expr.dynamic.cast.md)). However, if these operations are performed in a *ctor-initializer* (or in a function called directly or indirectly from a *ctor-initializer*) before all the *mem-initializer*s for base classes have completed, the program has undefined behavior. [*Example 11*:

```cpp
class A {
public:
  A(int);
};

class B : public A {
  int j;
public:
  int f();
  B() : A(f()),     // undefined behavior: calls member function but base A not yet initialized
  j(f()) { }        // well-defined: bases are all initialized
};

class C {
public:
  C(int);
};

class D : public B, C {
  int i;
public:
  D() : C(f()),     // undefined behavior: calls member function but base C not yet initialized
  i(f()) { }        // well-defined: bases are all initialized
};
```

— *end example*]

**17** [*Note 8*: [[class.cdtor]](class.cdtor.md) describes the result of virtual function calls, `typeid` and `dynamic_cast`s during construction for the well-defined cases; that is, describes the polymorphic behavior of an object under construction. — *end note*]

**18** A *mem-initializer* followed by an ellipsis is a pack expansion ([[temp.variadic]](../temp/temp.variadic.md)) that initializes the base classes specified by a pack expansion in the *base-specifier-list* for the class. [*Example 12*:

```cpp
template<class... Mixins>
class X : public Mixins... {
public:
  X(const Mixins&... mixins) : Mixins(mixins)... { }
};
```

— *end example*]
