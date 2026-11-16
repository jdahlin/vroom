# 11.4.8 Static members [class.static]

[Contents](../README.md) · up: [11.4 Class members \[class.mem\]](class.mem.md) · prev: [11.4.7.2 Conversion functions \[class.conv.fct\]](class.conv.fct.md) · next: [11.4.8.1 Static member functions \[class.static.mfct\]](class.static.mfct.md)

**1** A static member `s` of class `X` may be referred to using the *qualified-id* expression `X::s`; it is not necessary to use the class member access syntax ([[expr.ref]](../expr/expr.ref.md)) to refer to a static member. A static member may be referred to using the class member access syntax, in which case the object expression is evaluated. [*Example 1*:

```cpp
struct process {
  static void reschedule();
};
process& g();

void f() {
  process::reschedule();        // OK: no object necessary
  g().reschedule();             // g() is called
}
```

— *end example*]

**2** A static member may be referred to directly in the scope of its class or in the scope of a class derived ([[class.derived]](class.derived.md)) from its class; in this case, the static member is referred to as if a *qualified-id* expression was used, with the *nested-name-specifier* of the *qualified-id* naming the class scope from which the static member is referenced. [*Example 2*:

```cpp
int g();
struct X {
  static int g();
};
struct Y : X {
  static int i;
};
int Y::i = g();                 // equivalent to Y::g();
```

— *end example*]

**3** Static members obey the usual class member access rules ([[class.access]](class.access.md)). When used in the declaration of a class member, the `static` specifier shall only be used in the member declarations that appear within the *member-specification* of the class definition. [*Note 1*: It cannot be specified in member declarations that appear in namespace scope. — *end note*]

## Subclauses

- [11.4.8.1 Static member functions \[class.static.mfct\]](class.static.mfct.md)
- [11.4.8.2 Static data members \[class.static.data\]](class.static.data.md)
