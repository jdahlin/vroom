# 9.7.2 The `using enum` declaration [enum.udecl]

[Contents](../README.md) · up: [9.7 Enumerations \[enum\]](enum.md) · prev: [9.7.1 Enumeration declarations \[dcl.enum\]](dcl.enum.md) · next: [9.8 Namespaces \[basic.namespace\]](basic.namespace.md)

```bnf
using-enum-declaration:
    'using' elaborated-enum-specifier ';'
```

**1** The *elaborated-enum-specifier* shall not name a dependent type and the type shall have a reachable *enum-specifier*.

**2** A *using-enum-declaration* introduces the enumerator names of the named enumeration as if by a *using-declaration* for each enumerator.

**3** [*Note 1*: A *using-enum-declaration* in class scope adds the enumerators of the named enumeration as members to the scope. This means they are accessible for member lookup. [*Example 1*:

```cpp
enum class fruit { orange, apple };
struct S {
  using enum fruit;             // OK, introduces orange and apple into S
};
void f() {
  S s;
  s.orange;                     // OK, names fruit::orange
  S::orange;                    // OK, names fruit::orange
}
```

— *end example*] — *end note*]

**4** [*Note 2*: Two *using-enum-declaration*s that introduce two enumerators of the same name conflict. [*Example 2*:

```cpp
enum class fruit { orange, apple };
enum class color { red, orange };
void f() {
  using enum fruit;             // OK
  using enum color;             // error: color::orange and fruit::orange conflict
}
```

— *end example*] — *end note*]
