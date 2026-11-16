# 8.6.4 The range-based `for` statement [stmt.ranged]

[Contents](README.md) · up: [8.6 Iteration statements \[stmt.iter\]](stmt.iter.md) · prev: [8.6.3 The `for` statement \[stmt.for\]](stmt.for.md) · next: [8.7 Jump statements \[stmt.jump\]](stmt.jump.md)

**1** The range-based `for` statement

```bnf
'for' '(' init-statementₒₚₜ for-range-declaration ':' for-range-initializer ')' statement
```

is equivalent to

```bnf
'{'
    init-statementₒₚₜ
    'auto' '&&'range '=' for-range-initializer ';'
    'auto' begin '=' begin-expr ';'
    'auto' end '=' end-expr ';'
    'for' '(' ';' begin '!=' end';' '++'begin ')' '{'
        for-range-declaration '=' '*' begin ';'
        statement
    '}'
'}'
```

where

- if the *for-range-initializer* is an *expression*, it is regarded as if it were surrounded by parentheses (so that a comma operator cannot be reinterpreted as delimiting two *init-declarator*s);

- `range`, `begin`, and `end` are variables defined for exposition only; and

- *begin-expr* and *end-expr* are determined as follows:

  - if the *for-range-initializer* is an expression of array type `R`, *begin-expr* and *end-expr* are `range` and `range` `+` `N`, respectively, where `N` is the array bound. If `R` is an array of unknown bound or an array of incomplete type, the program is ill-formed;
  - if the *for-range-initializer* is an expression of class type `C`, the *unqualified-id*s `begin` and `end` are looked up in the scope of `C` as if by class member access lookup ([[basic.lookup.classref]](basic.lookup.classref.md)), and if both find at least one declaration, *begin-expr* and *end-expr* are `range.begin()` and `range.end()`, respectively;
  - otherwise, *begin-expr* and *end-expr* are `begin(range)` and `end(range)`, respectively, where `begin` and `end` are looked up in the associated namespaces ([[basic.lookup.argdep]](basic.lookup.argdep.md)). [*Note 1*: Ordinary unqualified lookup ([[basic.lookup.unqual]](basic.lookup.unqual.md)) is not performed. — *end note*]

[*Example 1*:

```cpp
int array[5] = { 1, 2, 3, 4, 5 };
for (int& x : array)
  x *= 2;
```

— *end example*]

**2** In the *decl-specifier-seq* of a *for-range-declaration*, each *decl-specifier* shall be either a *type-specifier* or `constexpr`. The *decl-specifier-seq* shall not define a class or enumeration.
