# 11.10.1 Explicit initialization [class.expl.init]

[Contents](../README.md) · up: [11.10 Initialization \[class.init\]](class.init.md) · prev: [11.10 Initialization \[class.init\]](class.init.md) · next: [11.10.2 Initializing bases and members \[class.base.init\]](class.base.init.md)

**1** An object of class type can be initialized with a parenthesized *expression-list*, where the *expression-list* is construed as an argument list for a constructor that is called to initialize the object. Alternatively, a single *assignment-expression* can be specified as an *initializer* using the `=` form of initialization. Either direct-initialization semantics or copy-initialization semantics apply; see [[dcl.init]](../dcl/dcl.init.md). [*Example 1*:

```cpp
struct complex {
  complex();
  complex(double);
  complex(double,double);
};

complex sqrt(complex,complex);

complex a(1);                   // initialized by calling complex(double) with argument 1
complex b = a;                  // initialized as a copy of a
complex c = complex(1,2);       // initialized by calling complex(double,double) with arguments 1 and 2
complex d = sqrt(b,c);          // initialized by calling sqrt(complex,complex) with d as its result object
complex e;                      // initialized by calling complex()
complex f = 3;                  // initialized by calling complex(double) with argument 3
complex g = { 1, 2 };           // initialized by calling complex(double, double) with arguments 1 and 2
```

— *end example*] [*Note 1*: Overloading of the assignment operator ([[over.ass]](../over/over.ass.md)) has no effect on initialization. — *end note*]

**2** An object of class type can also be initialized by a *braced-init-list*. List-initialization semantics apply; see [[dcl.init]](../dcl/dcl.init.md) and [[dcl.init.list]](../dcl/dcl.init.list.md). [*Example 2*:

```cpp
complex v[6] = { 1, complex(1,2), complex(), 2 };
```

Here, `complex::complex(double)` is called for the initialization of `v[0]` and `v[3]`, `complex::complex(double, double)` is called for the initialization of `v[1]`, `complex::complex()` is called for the initialization `v[2]`, `v[4]`, and `v[5]`. For another example,

```cpp
struct X {
  int i;
  float f;
  complex c;
} x = { 99, 88.8, 77.7 };
```

Here, `x.i` is initialized with 99, `x.f` is initialized with 88.8, and `complex::complex(double)` is called for the initialization of `x.c`. — *end example*] [*Note 2*: Braces can be elided in the *initializer-list* for any aggregate, even if the aggregate has members of a class type with user-defined type conversions; see [[dcl.init.aggr]](../dcl/dcl.init.aggr.md). — *end note*]

**3** [*Note 3*: If `T` is a class type with no default constructor, any declaration of an object of type `T` (or array thereof) is ill-formed if no *initializer* is explicitly specified (see [[class.init]](class.init.md) and [[dcl.init]](../dcl/dcl.init.md)). — *end note*]

**4** [*Note 4*: The order in which objects with static or thread storage duration are initialized is described in [[basic.start.dynamic]](../basic/basic.start.dynamic.md) and [[stmt.dcl]](../stmt/stmt.dcl.md). — *end note*]
