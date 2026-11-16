# 11.4.11 Nested type names [class.nested.type]

[Contents](../README.md) · up: [11.4 Class members \[class.mem\]](class.mem.md) · prev: [11.4.10 Nested class declarations \[class.nest\]](class.nest.md) · next: [11.5 Unions \[class.union\]](class.union.md)

**1** Type names obey exactly the same scope rules as other names. In particular, type names defined within a class definition cannot be used outside their class without qualification. [*Example 1*:

```cpp
struct X {
  typedef int I;
  class Y { /* ... */ };
  I a;
};

I b;                            // error
Y c;                            // error
X::Y d;                         // OK
X::I e;                         // OK
```

— *end example*]
