# 11.4.7 Conversions [class.conv]

[Contents](README.md) · up: [11.4 Class members \[class.mem\]](class.mem.md) · prev: [11.4.6 Destructors \[class.dtor\]](class.dtor.md) · next: [11.4.7.1 Conversion by constructor \[class.conv.ctor\]](class.conv.ctor.md)

**1** Type conversions of class objects can be specified by constructors and by conversion functions. These conversions are called *user-defined conversions* and are used for implicit type conversions ([[conv]](conv.md)), for initialization ([[dcl.init]](dcl.init.md)), and for explicit type conversions ([[expr.type.conv]](expr.type.conv.md), [[expr.cast]](expr.cast.md), [[expr.static.cast]](expr.static.cast.md)).

**2** User-defined conversions are applied only where they are unambiguous ([[class.member.lookup]](class.member.lookup.md), [[class.conv.fct]](class.conv.fct.md)). Conversions obey the access control rules ([[class.access]](class.access.md)). Access control is applied after ambiguity resolution ([[basic.lookup]](basic.lookup.md)).

**3** [*Note 1*: See [[over.match]](over.match.md) for a discussion of the use of conversions in function calls as well as examples below. — *end note*]

**4** At most one user-defined conversion (constructor or conversion function) is implicitly applied to a single value. [*Example 1*:

```cpp
struct X {
  operator int();
};

struct Y {
  operator X();
};

Y a;
int b = a;          // error: no viable conversion (a.operator X().operator int() not considered)
int c = X(a);       // OK: a.operator X().operator int()
```

— *end example*]

**5** User-defined conversions are used implicitly only if they are unambiguous. A conversion function in a derived class does not hide a conversion function in a base class unless the two functions convert to the same type. Function overload resolution ([[over.match.best]](over.match.best.md)) selects the best conversion function to perform the conversion. [*Example 2*:

```cpp
struct X {
  operator int();
};

struct Y : X {
    operator char();
};

void f(Y& a) {
  if (a) {          // error: ambiguous between X::operator int() and Y::operator char()
  }
}
```

— *end example*]

## Subclauses

- [11.4.7.1 Conversion by constructor \[class.conv.ctor\]](class.conv.ctor.md)
- [11.4.7.2 Conversion functions \[class.conv.fct\]](class.conv.fct.md)
