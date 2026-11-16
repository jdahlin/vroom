# 11.5.1 Anonymous unions [class.union.anon]

[Contents](../README.md) · up: [11.5 Unions \[class.union\]](class.union.md) · prev: [11.5 Unions \[class.union\]](class.union.md) · next: [11.6 Local class declarations \[class.local\]](class.local.md)

**1** A union of the form

```bnf
'union' '{' member-specification '}' ';'
```

is called an *anonymous union*; it defines an unnamed type and an unnamed object of that type called an *anonymous union object*. Each *member-declaration* in the *member-specification* of an anonymous union shall either define a non-static data member or be a *static\_assert-declaration*. Nested types, anonymous unions, and functions shall not be declared within an anonymous union. The names of the members of an anonymous union shall be distinct from the names of any other entity in the scope in which the anonymous union is declared. For the purpose of name lookup, after the anonymous union definition, the members of the anonymous union are considered to have been defined in the scope in which the anonymous union is declared. [*Example 1*:

```cpp
void f() {
  union { int a; const char* p; };
  a = 1;
  p = "Jennifer";
}
```

Here `a` and `p` are used like ordinary (non-member) variables, but since they are union members they have the same address. — *end example*]

**2** Anonymous unions declared in a named namespace or in the global namespace shall be declared `static`. Anonymous unions declared at block scope shall be declared with any storage class allowed for a block-scope variable, or with no storage class. A storage class is not allowed in a declaration of an anonymous union in a class scope. An anonymous union shall not have private or protected members ([[class.access]](class.access.md)). An anonymous union shall not have member functions.

**3** A union for which objects, pointers, or references are declared is not an anonymous union. [*Example 2*:

```cpp
void f() {
  union { int aa; char* p; } obj, *ptr = &obj;
  aa = 1;           // error
  ptr->aa = 1;      // OK
}
```

The assignment to plain `aa` is ill-formed since the member name is not visible outside the union, and even if it were visible, it is not associated with any particular object. — *end example*] [*Note 1*: Initialization of unions with no user-declared constructors is described in [[dcl.init.aggr]](../dcl/dcl.init.aggr.md). — *end note*]

**4** A *union-like class* is a union or a class that has an anonymous union as a direct member. A union-like class `X` has a set of *variant members*. If `X` is a union, a non-static data member of `X` that is not an anonymous union is a variant member of `X`. In addition, a non-static data member of an anonymous union that is a member of `X` is also a variant member of `X`. At most one variant member of a union may have a default member initializer. [*Example 3*:

```cpp
union U {
  int x = 0;
  union {
    int k;
  };
  union {
    int z;
    int y = 1;      // error: initialization for second variant member of U
  };
};
```

— *end example*]
