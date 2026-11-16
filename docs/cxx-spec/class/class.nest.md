# 11.4.10 Nested class declarations [class.nest]

[Contents](../README.md) · up: [11.4 Class members \[class.mem\]](class.mem.md) · prev: [11.4.9 Bit-fields \[class.bit\]](class.bit.md) · next: [11.4.11 Nested type names \[class.nested.type\]](class.nested.type.md)

**1** A class can be declared within another class. A class declared within another is called a *nested class*. The name of a nested class is local to its enclosing class. The nested class is in the scope of its enclosing class. [*Note 1*: See [[expr.prim.id]](../expr/expr.prim.id.md) for restrictions on the use of non-static data members and non-static member functions. — *end note*]

[*Example 1*:

```cpp
int x;
int y;

struct enclose {
  int x;
  static int s;

  struct inner {
    void f(int i) {
      int a = sizeof(x);        // OK: operand of sizeof is an unevaluated operand
      x = i;                    // error: assign to enclose::x
      s = i;                    // OK: assign to enclose::s
      ::x = i;                  // OK: assign to global x
      y = i;                    // OK: assign to global y
    }
    void g(enclose* p, int i) {
      p->x = i;                 // OK: assign to enclose::x
    }
  };
};

inner* p = 0;                   // error: inner not in scope
```

— *end example*]

**2** Member functions and static data members of a nested class can be defined in a namespace scope enclosing the definition of their class. [*Example 2*:

```cpp
struct enclose {
  struct inner {
    static int x;
    void f(int i);
  };
};

int enclose::inner::x = 1;

void enclose::inner::f(int i) { /* ... */ }
```

— *end example*]

**3** If class `X` is defined in a namespace scope, a nested class `Y` may be declared in class `X` and later defined in the definition of class `X` or be later defined in a namespace scope enclosing the definition of class `X`. [*Example 3*:

```cpp
class E {
  class I1;                     // forward declaration of nested class
  class I2;
  class I1 { };                 // definition of nested class
};
class E::I2 { };                // definition of nested class
```

— *end example*]

**4** Like a member function, a friend function ([[class.friend]](class.friend.md)) defined within a nested class is in the lexical scope of that class; it obeys the same rules for name binding as a static member function of that class ([[class.static]](class.static.md)), but it has no special access rights to members of an enclosing class.
