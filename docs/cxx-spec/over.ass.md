# 12.6.2.1 Simple assignment [over.ass]

[Contents](README.md) · up: [12.6.2 Binary operators \[over.binary\]](over.binary.md) · prev: [12.6.2 Binary operators \[over.binary\]](over.binary.md) · next: [12.6.3 Function call \[over.call\]](over.call.md)

**1** A *simple assignment operator function* is a binary operator function named `operator=`. A simple assignment operator function shall be a non-static member function. [*Note 1*: Because only standard conversion sequences are considered when converting to the left operand of an assignment operation ([[over.best.ics]](over.best.ics.md)), an expression `x = y` with a subexpression *x* of class type is always interpreted as `x.operator=(y)`. — *end note*]

**2** [*Note 2*: Since a copy assignment operator is implicitly declared for a class if not declared by the user ([[class.copy.assign]](class.copy.assign.md)), a base class assignment operator function is always hidden by the copy assignment operator function of the derived class. — *end note*]

**3** [*Note 3*: Any assignment operator function, even the copy and move assignment operators, can be virtual. For a derived class `D` with a base class `B` for which a virtual copy/move assignment has been declared, the copy/move assignment operator in `D` does not override `B`’s virtual copy/move assignment operator. [*Example 1*:

```cpp
struct B {
  virtual int operator= (int);
  virtual B& operator= (const B&);
};
struct D : B {
  virtual int operator= (int);
  virtual D& operator= (const B&);
};

D dobj1;
D dobj2;
B* bptr = &dobj1;
void f() {
  bptr->operator=(99);          // calls D::operator=(int)
  *bptr = 99;                   // ditto
  bptr->operator=(dobj2);       // calls D::operator=(const B&)
  *bptr = dobj2;                // ditto
  dobj1 = dobj2;                // calls implicitly-declared D::operator=(const D&)
}
```

— *end example*] — *end note*]
