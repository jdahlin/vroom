# 8.9 Ambiguity resolution [stmt.ambig]

[Contents](../README.md) · up: [8 Statements \[stmt.stmt\]](stmt.stmt.md) · prev: [8.8 Declaration statement \[stmt.dcl\]](stmt.dcl.md) · next: [9 Declarations \[dcl.dcl\]](../dcl/dcl.dcl.md)

**1** There is an ambiguity in the grammar involving *expression-statement*s and *declaration*s: An *expression-statement* with a function-style explicit type conversion ([[expr.type.conv]](../expr/expr.type.conv.md)) as its leftmost subexpression can be indistinguishable from a *declaration* where the first *declarator* starts with a `(`. In those cases the *statement* is a *declaration*.

**2** [*Note 1*: If the *statement* cannot syntactically be a *declaration*, there is no ambiguity, so this rule does not apply. The whole *statement* might need to be examined to determine whether this is the case. This resolves the meaning of many examples. [*Example 1*: Assuming `T` is a *simple-type-specifier* ([[dcl.type]](../dcl/dcl.type.md)),

```cpp
T(a)->m = 7;        // expression-statement
T(a)++;             // expression-statement
T(a,5)<<c;          // expression-statement

T(*d)(int);         //  declaration
T(e)[5];            //  declaration
T(f) = { 1, 2 };    //  declaration
T(*g)(double(3));   //  declaration
```

In the last example above, `g`, which is a pointer to `T`, is initialized to `double(3)`. This is of course ill-formed for semantic reasons, but that does not affect the syntactic analysis. — *end example*]

The remaining cases are *declaration*s. [*Example 2*:

```cpp
class T {
  // ...
public:
  T();
  T(int);
  T(int, int);
};
T(a);               //  declaration
T(*b)();            //  declaration
T(c)=7;             //  declaration
T(d),e,f=3;         //  declaration
extern int h;
T(g)(h,2);          //  declaration
```

— *end example*] — *end note*]

**3** The disambiguation is purely syntactic; that is, the meaning of the names occurring in such a statement, beyond whether they are *type-name*s or not, is not generally used in or changed by the disambiguation. Class templates are instantiated as necessary to determine if a qualified name is a *type-name*. Disambiguation precedes parsing, and a statement disambiguated as a declaration may be an ill-formed declaration. If, during parsing, a name in a template parameter is bound differently than it would be bound during a trial parse, the program is ill-formed. No diagnostic is required. [*Note 2*: This can occur only when the name is declared earlier in the declaration. — *end note*] [*Example 3*:

```cpp
struct T1 {
  T1 operator()(int x) { return T1(x); }
  int operator=(int x) { return x; }
  T1(int) { }
};
struct T2 { T2(int){ } };
int a, (*(*b)(T2))(int), c, d;

void f() {
  // disambiguation requires this to be parsed as a declaration:
  T1(a) = 3,
  T2(4),                        // T2 will be declared as a variable of type T1, but this will not
  (*(*b)(T2(c)))(int(d));       // allow the last part of the declaration to parse properly,
                                // since it depends on T2 being a type-name
}
```

— *end example*]
