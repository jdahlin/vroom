# 11.10.3 Initialization by inherited constructor [class.inhctor.init]

[Contents](../README.md) · up: [11.10 Initialization \[class.init\]](class.init.md) · prev: [11.10.2 Initializing bases and members \[class.base.init\]](class.base.init.md) · next: [11.10.4 Construction and destruction \[class.cdtor\]](class.cdtor.md)

**1** When a constructor for type `B` is invoked to initialize an object of a different type `D` (that is, when the constructor was inherited ([[namespace.udecl]](../dcl/namespace.udecl.md))), initialization proceeds as if a defaulted default constructor were used to initialize the `D` object and each base class subobject from which the constructor was inherited, except that the `B` subobject is initialized by the invocation of the inherited constructor. The complete initialization is considered to be a single function call; in particular, the initialization of the inherited constructor’s parameters is sequenced before the initialization of any part of the `D` object. [*Example 1*:

```cpp
struct B1 {
  B1(int, ...) { }
};

struct B2 {
  B2(double) { }
};

int get();

struct D1 : B1 {
  using B1::B1;     // inherits B1(int, ...)
  int x;
  int y = get();
};

void test() {
  D1 d(2, 3, 4);    // OK: B1 is initialized by calling B1(2, 3, 4),
                    // then d.x is default-initialized (no initialization is performed),
                    // then d.y is initialized by calling get()
  D1 e;             // error: D1 has a deleted default constructor
}

struct D2 : B2 {
  using B2::B2;
  B1 b;
};

D2 f(1.0);          // error: B1 has a deleted default constructor

struct W { W(int); };
struct X : virtual W { using W::W; X() = delete; };
struct Y : X { using X::X; };
struct Z : Y, virtual W { using Y::Y; };
Z z(0);             // OK: initialization of Y does not invoke default constructor of X

template<class T> struct Log : T {
  using T::T;       // inherits all constructors from class T
  ~Log() { std::clog << "Destroying wrapper" << std::endl; }
};
```

Class template `Log` wraps any class and forwards all of its constructors, while writing a message to the standard log whenever an object of class `Log` is destroyed. — *end example*]

**2** If the constructor was inherited from multiple base class subobjects of type `B`, the program is ill-formed. [*Example 2*:

```cpp
struct A { A(int); };
struct B : A { using A::A; };

struct C1 : B { using B::B; };
struct C2 : B { using B::B; };

struct D1 : C1, C2 {
  using C1::C1;
  using C2::C2;
};

struct V1 : virtual B { using B::B; };
struct V2 : virtual B { using B::B; };

struct D2 : V1, V2 {
  using V1::V1;
  using V2::V2;
};

D1 d1(0);           // error: ambiguous
D2 d2(0);           // OK: initializes virtual B base class, which initializes the A base class
                    // then initializes the V1 and V2 base classes as if by a defaulted default constructor

struct M { M(); M(int); };
struct N : M { using M::M; };
struct O : M {};
struct P : N, O { using N::N; using O::O; };
P p(0);             // OK: use M(0) to initialize N's base class,
                    // use M() to initialize O's base class
```

— *end example*]

**3** When an object is initialized by an inherited constructor, initialization of the object is complete when the initialization of all subobjects is complete.
