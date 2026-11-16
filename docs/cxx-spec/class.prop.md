# 11.2 Properties of classes [class.prop]

[Contents](README.md) · up: [11 Classes \[class\]](class.md) · prev: [11.1 Preamble \[class.pre\]](class.pre.md) · next: [11.3 Class names \[class.name\]](class.name.md)

**1** A *trivially copyable class* is a class:

- that has at least one eligible copy constructor, move constructor, copy assignment operator, or move assignment operator ([[special]](special.md), [[class.copy.ctor]](class.copy.ctor.md), [[class.copy.assign]](class.copy.assign.md)),
- where each eligible copy constructor, move constructor, copy assignment operator, and move assignment operator is trivial, and
- that has a trivial, non-deleted destructor ([[class.dtor]](class.dtor.md)).

**2** A *trivial class* is a class that is trivially copyable and has one or more eligible default constructors ([[class.default.ctor]](class.default.ctor.md)), all of which are trivial. [*Note 1*: In particular, a trivially copyable or trivial class does not have virtual functions or virtual base classes. — *end note*]

**3** A class `S` is a *standard-layout class* if it:

- has no non-static data members of type non-standard-layout class (or array of such types) or reference,

- has no virtual functions ([[class.virtual]](class.virtual.md)) and no virtual base classes ([[class.mi]](class.mi.md)),

- has the same access control ([[class.access]](class.access.md)) for all non-static data members,

- has no non-standard-layout base classes,

- has at most one base class subobject of any given type,

- has all non-static data members and bit-fields in the class and its base classes first declared in the same class, and

- has no element of the set *M*(`S`) of types as a base class, where for any type `X`, *M*(`X`) is defined as follows.[^1] [*Note 2*: *M*(`X`) is the set of the types of all non-base-class subobjects that may be at a zero offset in `X`. — *end note*]

  - If `X` is a non-union class type with no (possibly inherited ([[class.derived]](class.derived.md))) non-static data members, the set *M*(`X`) is empty.
  - If `X` is a non-union class type with a non-static data member of type `X`<sub>0</sub> that is either of zero size or is the first non-static data member of `X` (where said member may be an anonymous union), the set *M*(`X`) consists of `X`<sub>0</sub> and the elements of *M*(`X`<sub>0</sub>).
  - If `X` is a union type, the set *M*(`X`) is the union of all *M*(`U`<sub>*i*</sub>) and the set containing all `U`<sub>*i*</sub>, where each `U`<sub>*i*</sub> is the type of the *i*<sup></sup>*th* non-static data member of `X`.
  - If `X` is an array type with element type `X`<sub>*e*</sub>, the set *M*(`X`) consists of `X`<sub>*e*</sub> and the elements of *M*(`X`<sub>*e*</sub>).
  - If `X` is a non-class, non-array type, the set *M*(`X`) is empty.

**4** [*Example 1*:

```cpp
struct B { int i; };            // standard-layout class
struct C : B { };               // standard-layout class
struct D : C { };               // standard-layout class
struct E : D { char : 4; };     // not a standard-layout class

struct Q {};
struct S : Q { };
struct T : Q { };
struct U : S, T { };            // not a standard-layout class
```

— *end example*]

**5** A *standard-layout struct* is a standard-layout class defined with the *class-key* `struct` or the *class-key* `class`. A *standard-layout union* is a standard-layout class defined with the *class-key* `union`.

**6** [*Note 3*: Standard-layout classes are useful for communicating with code written in other programming languages. Their layout is specified in [[class.mem]](class.mem.md). — *end note*]

**7** [*Example 2*:

```cpp
struct N {          // neither trivial nor standard-layout
  int i;
  int j;
  virtual ~N();
};

struct T {          // trivial but not standard-layout
  int i;
private:
  int j;
};

struct SL {         // standard-layout but not trivial
  int i;
  int j;
  ~SL();
};

struct POD {        // both trivial and standard-layout
  int i;
  int j;
};
```

— *end example*]

**8** [*Note 4*: Aggregates of class type are described in [[dcl.init.aggr]](dcl.init.aggr.md). — *end note*]

**9** A class `S` is an *implicit-lifetime class* if it is an aggregate or has at least one trivial eligible constructor and a trivial, non-deleted destructor.

[^1]: This ensures that two subobjects that have the same class type and that belong to the same most derived object are not allocated at the same address ([[expr.eq]](expr.eq.md)).
