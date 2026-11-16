# 11.4.7.2 Conversion functions [class.conv.fct]

[Contents](README.md) · up: [11.4.7 Conversions \[class.conv\]](class.conv.md) · prev: [11.4.7.1 Conversion by constructor \[class.conv.ctor\]](class.conv.ctor.md) · next: [11.4.8 Static members \[class.static\]](class.static.md)

**1** A member function of a class `X` having no parameters with a name of the form

```bnf
conversion-function-id:
    'operator' conversion-type-id
```

```bnf
conversion-type-id:
    type-specifier-seq conversion-declaratorₒₚₜ
```

```bnf
conversion-declarator:
    ptr-operator conversion-declaratorₒₚₜ
```

specifies a conversion from `X` to the type specified by the *conversion-type-id*. Such functions are called *conversion functions*. A *decl-specifier* in the *decl-specifier-seq* of a conversion function (if any) shall be neither a *defining-type-specifier* nor `static`. The type of the conversion function ([[dcl.fct]](dcl.fct.md)) is “function taking no parameter returning *conversion-type-id*”. A conversion function is never used to convert a (possibly cv-qualified) object to the (possibly cv-qualified) same object type (or a reference to it), to a (possibly cv-qualified) base class of that type (or a reference to it), or to *cv* `void`.[^1] [*Example 1*:

```cpp
struct X {
  operator int();
  operator auto() -> short;     // error: trailing return type
};

void f(X a) {
  int i = int(a);
  i = (int)a;
  i = a;
}
```

In all three cases the value assigned will be converted by `X::operator int()`. — *end example*]

**2** A conversion function may be explicit ([[dcl.fct.spec]](dcl.fct.spec.md)), in which case it is only considered as a user-defined conversion for direct-initialization ([[dcl.init]](dcl.init.md)). Otherwise, user-defined conversions are not restricted to use in assignments and initializations. [*Example 2*:

```cpp
class Y { };
struct Z {
  explicit operator Y() const;
};

void h(Z z) {
  Y y1(z);          // OK: direct-initialization
  Y y2 = z;         // error: no conversion function candidate for copy-initialization
  Y y3 = (Y)z;      // OK: cast notation
}

void g(X a, X b) {
  int i = (a) ? 1+a : 0;
  int j = (a&&b) ? a+b : i;
  if (a) {
  }
}
```

— *end example*]

**3** The *conversion-type-id* shall not represent a function type nor an array type. The *conversion-type-id* in a *conversion-function-id* is the longest sequence of tokens that could possibly form a *conversion-type-id*. [*Note 1*: This prevents ambiguities between the declarator operator `*` and its expression counterparts. [*Example 3*:

```cpp
&ac.operator int*i; // syntax error:
                    // parsed as: &(ac.operator int *)i
                    // not as: &(ac.operator int)*i
```

The `*` is the pointer declarator and not the multiplication operator. — *end example*] This rule also prevents ambiguities for attributes. [*Example 4*:

```cpp
operator int [[noreturn]] ();   // error: noreturn attribute applied to a type
```

— *end example*] — *end note*]

**4** Conversion functions are inherited.

**5** Conversion functions can be virtual.

**6** A conversion function template shall not have a deduced return type ([[dcl.spec.auto]](dcl.spec.auto.md)). [*Example 5*:

```cpp
struct S {
  operator auto() const { return 10; }      // OK
  template<class T>
  operator auto() const { return 1.2; }     // error: conversion function template
};
```

— *end example*]

[^1]: These conversions are considered as standard conversions for the purposes of overload resolution ([[over.best.ics]](over.best.ics.md), [[over.ics.ref]](over.ics.ref.md)) and therefore initialization ([[dcl.init]](dcl.init.md)) and explicit casts ([[expr.static.cast]](expr.static.cast.md)). A conversion to `void` does not invoke any conversion function ([[expr.static.cast]](expr.static.cast.md)). Even though never directly called to perform a conversion, such conversion functions can be declared and can potentially be reached through a call to a virtual conversion function in a base class.
