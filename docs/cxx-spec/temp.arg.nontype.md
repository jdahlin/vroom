# 13.4.2 Template non-type arguments [temp.arg.nontype]

[Contents](README.md) · up: [13.4 Template arguments \[temp.arg\]](temp.arg.md) · prev: [13.4.1 Template type arguments \[temp.arg.type\]](temp.arg.type.md) · next: [13.4.3 Template template arguments \[temp.arg.template\]](temp.arg.template.md)

**1** If the type `T` of a *template-parameter* ([[temp.param]](temp.param.md)) contains a placeholder type ([[dcl.spec.auto]](dcl.spec.auto.md)) or a placeholder for a deduced class type ([[dcl.type.class.deduct]](dcl.type.class.deduct.md)), the type of the parameter is the type deduced for the variable `x` in the invented declaration

```cpp
T x = template-argument ;
```

If a deduced parameter type is not permitted for a *template-parameter* declaration ([[temp.param]](temp.param.md)), the program is ill-formed.

**2** A *template-argument* for a non-type *template-parameter* shall be a converted constant expression ([[expr.const]](expr.const.md)) of the type of the *template-parameter*. [*Note 1*: If the *template-argument* is an overload set (or the address of such, including forming a pointer-to-member), the matching function is selected from the set ([[over.over]](over.over.md)). — *end note*]

**3** For a non-type *template-parameter* of reference or pointer type, or for each non-static data member of reference or pointer type in a non-type *template-parameter* of class type or subobject thereof, the reference or pointer value shall not refer to or be the address of (respectively):

- a temporary object ([[class.temporary]](class.temporary.md)),
- a string literal object ([[lex.string]](lex.string.md)),
- the result of a `typeid` expression ([[expr.typeid]](expr.typeid.md)),
- a predefined `__func__` variable ([[dcl.fct.def.general]](dcl.fct.def.general.md)), or
- a subobject ([[intro.object]](intro.object.md)) of one of the above.

**4** [*Example 1*:

```cpp
template<const int* pci> struct X { /* ... */ };
int ai[10];
X<ai> xi;                       // array to pointer and qualification conversions

struct Y { /* ... */ };
template<const Y& b> struct Z { /* ... */ };
Y y;
Z<y> z;                         // no conversion, but note extra cv-qualification

template<int (&pa)[5]> struct W { /* ... */ };
int b[5];
W<b> w;                         // no conversion

void f(char);
void f(int);

template<void (*pf)(int)> struct A { /* ... */ };

A<&f> a;                        // selects f(int)

template<auto n> struct B { /* ... */ };
B<5> b1;                        // OK, template parameter type is int
B<'a'> b2;                      // OK, template parameter type is char
B<2.5> b3;                      // OK, template parameter type is double
B<void(0)> b4;                  // error: template parameter type cannot be void
```

— *end example*]

**5** [*Note 2*: A *string-literal* ([[lex.string]](lex.string.md)) is not an acceptable *template-argument* for a *template-parameter* of non-class type. [*Example 2*:

```cpp
template<class T, T p> class X {
  /* ... */
};

X<const char*, "Studebaker"> x; // error: string literal object as template-argument
X<const char*, "Knope" + 1> x2; // error: subobject of string literal object as template-argument

const char p[] = "Vivisectionist";
X<const char*, p> y;            // OK

struct A {
  constexpr A(const char*) {}
};

X<A, "Pyrophoricity"> z;        // OK, string-literal is a constructor argument to A
```

— *end example*] — *end note*]

**6** [*Note 3*: A temporary object is not an acceptable *template-argument* when the corresponding *template-parameter* has reference type. [*Example 3*:

```cpp
template<const int& CRI> struct B { /* ... */ };

B<1> b1;                        // error: temporary would be required for template argument

int c = 1;
B<c> b2;                        // OK

struct X { int n; };
struct Y { const int &r; };
template<Y y> struct C { /* ... */ };
C<Y{X{1}.n}> c;                 // error: subobject of temporary object used to initialize
                                // reference member of template parameter
```

— *end example*] — *end note*]
