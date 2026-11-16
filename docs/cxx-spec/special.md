# 11.4.3 Special member functions [special]

[Contents](README.md) · up: [11.4 Class members \[class.mem\]](class.mem.md) · prev: [11.4.2.1 The `this` pointer \[class.this\]](class.this.md) · next: [11.4.4 Constructors \[class.ctor\]](class.ctor.md)

**1** Default constructors ([[class.default.ctor]](class.default.ctor.md)), copy constructors, move constructors ([[class.copy.ctor]](class.copy.ctor.md)), copy assignment operators, move assignment operators ([[class.copy.assign]](class.copy.assign.md)), and prospective destructors ([[class.dtor]](class.dtor.md)) are *special member functions*. [*Note 1*: The implementation will implicitly declare these member functions for some class types when the program does not explicitly declare them. The implementation will implicitly define them if they are odr-used ([[basic.def.odr]](basic.def.odr.md)) or needed for constant evaluation ([[expr.const]](expr.const.md)). — *end note*] An implicitly-declared special member function is declared at the closing `}` of the *class-specifier*. Programs shall not define implicitly-declared special member functions.

**2** Programs may explicitly refer to implicitly-declared special member functions. [*Example 1*: A program may explicitly call or form a pointer to member to an implicitly-declared special member function.

```cpp
struct A { };                   // implicitly declared A::operator=
struct B : A {
  B& operator=(const B &);
};
B& B::operator=(const B& s) {
  this->A::operator=(s);        // well-formed
  return *this;
}
```

— *end example*]

**3** [*Note 2*: The special member functions affect the way objects of class type are created, copied, moved, and destroyed, and how values can be converted to values of other types. Often such special member functions are called implicitly. — *end note*]

**4** Special member functions obey the usual access rules ([[class.access]](class.access.md)). [*Example 2*: Declaring a constructor protected ensures that only derived classes and friends can create objects using it. — *end example*]

**5** Two special member functions are of the same kind if:

- they are both default constructors,
- they are both copy or move constructors with the same first parameter type, or
- they are both copy or move assignment operators with the same first parameter type and the same *cv-qualifier*s and *ref-qualifier*, if any.

**6** An *eligible special member function* is a special member function for which:

- the function is not deleted,
- the associated constraints ([[temp.constr]](temp.constr.md)), if any, are satisfied, and
- no special member function of the same kind is more constrained ([[temp.constr.order]](temp.constr.order.md)).

**7** For a class, its non-static data members, its non-virtual direct base classes, and, if the class is not abstract ([[class.abstract]](class.abstract.md)), its virtual base classes are called its *potentially constructed subobjects*.

**8** A defaulted special member function is *constexpr-compatible* if the corresponding implicitly-declared special member function would be a constexpr function.
