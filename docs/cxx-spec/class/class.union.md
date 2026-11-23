# 11.5 Unions [class.union]

[Contents](../README.md) · up: [11 Classes \[class\]](class.md) · prev: [11.4.11 Nested type names \[class.nested.type\]](class.nested.type.md) · next: [11.5.1 Anonymous unions \[class.union.anon\]](class.union.anon.md)

**1** A *union* is a class defined with the *class-key* `union`.

**2** In a union, a non-static data member is *active* if its name refers to an object whose lifetime has begun and has not ended ([[basic.life]](../basic/basic.life.md)). At most one of the non-static data members of an object of union type can be active at any time, that is, the value of at most one of the non-static data members can be stored in a union at any time. [*Note 1*: One special guarantee is made in order to simplify the use of unions: If a standard-layout union contains several standard-layout structs that share a common initial sequence ([[class.mem]](class.mem.md)), and if a non-static data member of an object of this standard-layout union type is active and is one of the standard-layout structs, it is permitted to inspect the common initial sequence of any of the standard-layout struct members; see [[class.mem]](class.mem.md). — *end note*]

**3** The size of a union is sufficient to contain the largest of its non-static data members. Each non-static data member is allocated as if it were the sole member of a non-union class. [*Note 2*: A union object and its non-static data members are pointer-interconvertible ([[basic.compound]](../basic/basic.compound.md), [[expr.static.cast]](../expr/expr.static.cast.md)). As a consequence, all non-static data members of a union object have the same address. — *end note*]

**4** A union can have member functions (including constructors and destructors), but it shall not have virtual ([[class.virtual]](class.virtual.md)) functions. A union shall not have base classes. A union shall not be used as a base class. If a union contains a non-static data member of reference type the program is ill-formed. [*Note 3*: Absent default member initializers ([[class.mem]](class.mem.md)), if any non-static data member of a union has a non-trivial default constructor ([[class.default.ctor]](class.default.ctor.md)), copy constructor, move constructor ([[class.copy.ctor]](class.copy.ctor.md)), copy assignment operator, move assignment operator ([[class.copy.assign]](class.copy.assign.md)), or destructor ([[class.dtor]](class.dtor.md)), the corresponding member function of the union must be user-provided or it will be implicitly deleted ([[dcl.fct.def.delete]](../dcl/dcl.fct.def.delete.md)) for the union. — *end note*]

**5** [*Example 1*: Consider the following union:

```cpp
union U {
  int i;
  float f;
  std::string s;
};
```

Since `std::string` ([[string.classes]](../lib/strings/string.classes.md)) declares non-trivial versions of all of the special member functions, `U` will have an implicitly deleted default constructor, copy/move constructor, copy/move assignment operator, and destructor. To use `U`, some or all of these member functions must be user-provided. — *end example*]

**6** When the left operand of an assignment operator involves a member access expression ([[expr.ref]](../expr/expr.ref.md)) that nominates a union member, it may begin the lifetime of that union member, as described below. For an expression `E`, define the set *S*(`E`) of subexpressions of `E` as follows:

- If `E` is of the form `A.B`, *S*(`E`) contains the elements of *S*(`A`), and also contains `A.B` if `B` names a union member of a non-class, non-array type, or of a class type with a trivial default constructor that is not deleted, or an array of such types.
- If `E` is of the form `A[B]` and is interpreted as a built-in array subscripting operator, *S*(`E`) is *S*(`A`) if `A` is of array type, *S*(`B`) if `B` is of array type, and empty otherwise.
- Otherwise, *S*(`E`) is empty.

In an assignment expression of the form `E1 = E2` that uses either the built-in assignment operator ([[expr.ass]](../expr/expr.ass.md)) or a trivial assignment operator ([[class.copy.assign]](class.copy.assign.md)), for each element `X` of *S*(`E1`), if modification of `X` would have undefined behavior under [[basic.life]](../basic/basic.life.md), an object of the type of `X` is implicitly created in the nominated storage; no initialization is performed and the beginning of its lifetime is sequenced after the value computation of the left and right operands and before the assignment. [*Note 4*: This ends the lifetime of the previously-active member of the union, if any ([[basic.life]](../basic/basic.life.md)). — *end note*] [*Example 2*:

```cpp
union A { int x; int y[4]; };
struct B { A a; };
union C { B b; int k; };
int f() {
  C c;                  // does not start lifetime of any union member
  c.b.a.y[3] = 4;       // OK: S(c.b.a.y[3]) contains c.b and c.b.a.y;
                        // creates objects to hold union members c.b and c.b.a.y
  return c.b.a.y[3];    // OK: c.b.a.y refers to newly created object (see [basic.life])
}

struct X { const int a; int b; };
union Y { X x; int k; };
void g() {
  Y y = { { 1, 2 } };   // OK, y.x is active union member ([class.mem])
  int n = y.x.a;
  y.k = 4;              // OK: ends lifetime of y.x, y.k is active member of union
  y.x.b = n;            // undefined behavior: y.x.b modified outside its lifetime,
                        // S(y.x.b) is empty because X's default constructor is deleted,
                        // so union member y.x's lifetime does not implicitly start
}
```

— *end example*]

**7** [*Note 5*: In general, one must use explicit destructor calls and placement *new-expression* to change the active member of a union. — *end note*] [*Example 3*: Consider an object `u` of a `union` type `U` having non-static data members `m` of type `M` and `n` of type `N`. If `M` has a non-trivial destructor and `N` has a non-trivial constructor (for instance, if they declare or inherit virtual functions), the active member of `u` can be safely switched from `m` to `n` using the destructor and placement *new-expression* as follows:

```cpp
u.m.~M();
new (&u.n) N;
```

— *end example*]

## Subclauses

- [11.5.1 Anonymous unions \[class.union.anon\]](class.union.anon.md)
