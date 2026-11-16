# 9.7.1 Enumeration declarations [dcl.enum]

[Contents](README.md) · up: [9.7 Enumerations \[enum\]](enum.md) · prev: [9.7 Enumerations \[enum\]](enum.md) · next: [9.7.2 The `using enum` declaration \[enum.udecl\]](enum.udecl.md)

**1** An enumeration is a distinct type ([[basic.compound]](basic.compound.md)) with named constants. Its name becomes an *enum-name* within its scope.

```bnf
enum-name:
    identifier
```

```bnf
enum-specifier:
    enum-head '{' enumerator-listₒₚₜ '}'
    enum-head '{' enumerator-list ',' '}'
```

```bnf
enum-head:
    enum-key attribute-specifier-seqₒₚₜ enum-head-nameₒₚₜ enum-baseₒₚₜ
```

```bnf
enum-head-name:
    nested-name-specifierₒₚₜ identifier
```

```bnf
opaque-enum-declaration:
    enum-key attribute-specifier-seqₒₚₜ enum-head-name enum-baseₒₚₜ ';'
```

```bnf
enum-key:
    'enum'
    'enum' 'class'
    'enum' 'struct'
```

```bnf
enum-base:
    ':' type-specifier-seq
```

```bnf
enumerator-list:
    enumerator-definition
    enumerator-list ',' enumerator-definition
```

```bnf
enumerator-definition:
    enumerator
    enumerator '=' constant-expression
```

```bnf
enumerator:
    identifier attribute-specifier-seqₒₚₜ
```

The optional *attribute-specifier-seq* in the *enum-head* and the *opaque-enum-declaration* appertains to the enumeration; the attributes in that *attribute-specifier-seq* are thereafter considered attributes of the enumeration whenever it is named. A `:` following “`enum` *nested-name-specifier*ₒₚₜ *identifier*” within the *decl-specifier-seq* of a *member-declaration* is parsed as part of an *enum-base*. [*Note 1*: This resolves a potential ambiguity between the declaration of an enumeration with an *enum-base* and the declaration of an unnamed bit-field of enumeration type. [*Example 1*:

```cpp
struct S {
  enum E : int {};
  enum E : int {};              // error: redeclaration of enumeration
};
```

— *end example*] — *end note*] If the *enum-head-name* of an *opaque-enum-declaration* contains a *nested-name-specifier*, the declaration shall be an explicit specialization ([[temp.expl.spec]](temp.expl.spec.md)).

**2** The enumeration type declared with an *enum-key* of only `enum` is an *unscoped enumeration*, and its *enumerator*s are *unscoped enumerators*. The *enum-key*s `enum class` and `enum struct` are semantically equivalent; an enumeration type declared with one of these is a *scoped enumeration*, and its *enumerator*s are *scoped enumerators*. The optional *enum-head-name* shall not be omitted in the declaration of a scoped enumeration. The *type-specifier-seq* of an *enum-base* shall name an integral type; any cv-qualification is ignored. An *opaque-enum-declaration* declaring an unscoped enumeration shall not omit the *enum-base*. The identifiers in an *enumerator-list* are declared as constants, and can appear wherever constants are required. An *enumerator-definition* with `=` gives the associated *enumerator* the value indicated by the *constant-expression*. If the first *enumerator* has no *initializer*, the value of the corresponding constant is zero. An *enumerator-definition* without an *initializer* gives the *enumerator* the value obtained by increasing the value of the previous *enumerator* by one. [*Example 2*:

```cpp
enum { a, b, c=0 };
enum { d, e, f=e+2 };
```

defines `a`, `c`, and `d` to be zero, `b` and `e` to be `1`, and `f` to be `3`. — *end example*] The optional *attribute-specifier-seq* in an *enumerator* appertains to that enumerator.

**3** An *opaque-enum-declaration* is either a redeclaration of an enumeration in the current scope or a declaration of a new enumeration. [*Note 2*: An enumeration declared by an *opaque-enum-declaration* has a fixed underlying type and is a complete type. The list of enumerators can be provided in a later redeclaration with an *enum-specifier*. — *end note*] A scoped enumeration shall not be later redeclared as unscoped or with a different underlying type. An unscoped enumeration shall not be later redeclared as scoped and each redeclaration shall include an *enum-base* specifying the same underlying type as in the original declaration.

**4** If an *enum-head-name* contains a *nested-name-specifier*, it shall not begin with a *decltype-specifier* and the enclosing *enum-specifier* or *opaque-enum-declaration* shall refer to an enumeration that was previously declared directly in the class or namespace to which the *nested-name-specifier* refers, or in an element of the inline namespace set ([[namespace.def]](namespace.def.md)) of that namespace (i.e., neither inherited nor introduced by a *using-declaration*), and the *enum-specifier* or *opaque-enum-declaration* shall appear in a namespace enclosing the previous declaration.

**5** Each enumeration defines a type that is different from all other types. Each enumeration also has an *underlying type*. The underlying type can be explicitly specified using an *enum-base*. For a scoped enumeration type, the underlying type is `int` if it is not explicitly specified. In both of these cases, the underlying type is said to be *fixed*. Following the closing brace of an *enum-specifier*, each enumerator has the type of its enumeration. If the underlying type is fixed, the type of each enumerator prior to the closing brace is the underlying type and the *constant-expression* in the *enumerator-definition* shall be a converted constant expression of the underlying type ([[expr.const]](expr.const.md)). If the underlying type is not fixed, the type of each enumerator prior to the closing brace is determined as follows:

- If an initializer is specified for an enumerator, the *constant-expression* shall be an integral constant expression ([[expr.const]](expr.const.md)). If the expression has unscoped enumeration type, the enumerator has the underlying type of that enumeration type, otherwise it has the same type as the expression.
- If no initializer is specified for the first enumerator, its type is an unspecified signed integral type.
- Otherwise the type of the enumerator is the same as that of the preceding enumerator unless the incremented value is not representable in that type, in which case the type is an unspecified integral type sufficient to contain the incremented value. If no such type exists, the program is ill-formed.

**6** An enumeration whose underlying type is fixed is an incomplete type from its point of declaration ([[basic.scope.pdecl]](basic.scope.pdecl.md)) to immediately after its *enum-base* (if any), at which point it becomes a complete type. An enumeration whose underlying type is not fixed is an incomplete type from its point of declaration to immediately after the closing `}` of its *enum-specifier*, at which point it becomes a complete type.

**7** For an enumeration whose underlying type is not fixed, the underlying type is an integral type that can represent all the enumerator values defined in the enumeration. If no integral type can represent all the enumerator values, the enumeration is ill-formed. It is implementation-defined which integral type is used as the underlying type except that the underlying type shall not be larger than `int` unless the value of an enumerator cannot fit in an `int` or `unsigned int`. If the *enumerator-list* is empty, the underlying type is as if the enumeration had a single enumerator with value 0.

**8** For an enumeration whose underlying type is fixed, the values of the enumeration are the values of the underlying type. Otherwise, the values of the enumeration are the values representable by a hypothetical integer type with minimal width *M* such that all enumerators can be represented. The width of the smallest bit-field large enough to hold all the values of the enumeration type is *M*. It is possible to define an enumeration that has values not defined by any of its enumerators. If the *enumerator-list* is empty, the values of the enumeration are as if the enumeration had a single enumerator with value 0.[^1]

**9** Two enumeration types are *layout-compatible enumerations* if they have the same underlying type.

**10** The value of an enumerator or an object of an unscoped enumeration type is converted to an integer by integral promotion ([[conv.prom]](conv.prom.md)). [*Example 3*:

```cpp
enum color { red, yellow, green=20, blue };
color col = red;
color* cp = &col;
if (*cp == blue)                // ...
```

makes `color` a type describing various colors, and then declares `col` as an object of that type, and `cp` as a pointer to an object of that type. The possible values of an object of type `color` are `red`, `yellow`, `green`, `blue`; these values can be converted to the integral values `0`, `1`, `20`, and `21`. Since enumerations are distinct types, objects of type `color` can be assigned only values of type `color`.

```cpp
color c = 1;                    // error: type mismatch, no conversion from int to color
int i = yellow;                 // OK: yellow converted to integral value 1, integral promotion
```

Note that this implicit `enum` to `int` conversion is not provided for a scoped enumeration:

```cpp
enum class Col { red, yellow, green };
int x = Col::red;               // error: no Col to int conversion
Col y = Col::red;
if (y) { }                      // error: no Col to bool conversion
```

— *end example*]

**11** Each *enum-name* and each unscoped *enumerator* is declared in the scope that immediately contains the *enum-specifier*. Each scoped *enumerator* is declared in the scope of the enumeration. An unnamed enumeration that does not have a typedef name for linkage purposes ([[dcl.typedef]](dcl.typedef.md)) and that has a first enumerator is denoted, for linkage purposes ([[basic.link]](basic.link.md)), by its underlying type and its first enumerator; such an enumeration is said to have an enumerator as a name for linkage purposes. These names obey the scope rules defined for all names in [[basic.scope]](basic.scope.md) and [[basic.lookup]](basic.lookup.md). [*Note 3*: Each unnamed enumeration with no enumerators is a distinct type. — *end note*] [*Example 4*:

```cpp
enum direction { left='l', right='r' };

void g()  {
  direction d;                  // OK
  d = left;                     // OK
  d = direction::right;         // OK
}

enum class altitude { high='h', low='l' };

void h()  {
  altitude a;                   // OK
  a = high;                     // error: high not in scope
  a = altitude::low;            // OK
}
```

— *end example*] An enumerator declared in class scope can be referred to using the class member access operators (`::`, `.` (dot) and `->` (arrow)), see [[expr.ref]](expr.ref.md). [*Example 5*:

```cpp
struct X {
  enum direction { left='l', right='r' };
  int f(int i) { return i==left ? 0 : i==right ? 1 : 2; }
};

void g(X* p) {
  direction d;                  // error: direction not in scope
  int i;
  i = p->f(left);               // error: left not in scope
  i = p->f(X::right);           // OK
  i = p->f(p->left);            // OK
  // ...
}
```

— *end example*]

[^1]: This set of values is used to define promotion and conversion semantics for the enumeration type. It does not preclude an expression of enumeration type from having a value that falls outside this range.
