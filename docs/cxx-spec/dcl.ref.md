# 9.3.3.2 References [dcl.ref]

[Contents](README.md) · up: [9.3.3 Meaning of declarators \[dcl.meaning\]](dcl.meaning.md) · prev: [9.3.3.1 Pointers \[dcl.ptr\]](dcl.ptr.md) · next: [9.3.3.3 Pointers to members \[dcl.mptr\]](dcl.mptr.md)

**1** In a declaration `T` `D` where `D` has either of the forms

```bnf
'&' attribute-specifier-seqₒₚₜ 'D1'
'&&' attribute-specifier-seqₒₚₜ 'D1'
```

and the type of the identifier in the declaration `T` `D1` is “*derived-declarator-type-list* `T`”, then the type of the identifier of `D` is “*derived-declarator-type-list* reference to `T`”. The optional *attribute-specifier-seq* appertains to the reference type. Cv-qualified references are ill-formed except when the cv-qualifiers are introduced through the use of a *typedef-name* ([[dcl.typedef]](dcl.typedef.md), [[temp.param]](temp.param.md)) or *decltype-specifier* ([[dcl.type.simple]](dcl.type.simple.md)), in which case the cv-qualifiers are ignored. [*Example 1*:

```cpp
typedef int& A;
const A aref = 3;   // error: lvalue reference to non-const initialized with rvalue
```

The type of `aref` is “lvalue reference to `int`”, not “lvalue reference to `const int`”. — *end example*] [*Note 1*: A reference can be thought of as a name of an object. — *end note*] A declarator that specifies the type “reference to *cv* `void`” is ill-formed.

**2** A reference type that is declared using `&` is called an *lvalue reference*, and a reference type that is declared using `&&` is called an *rvalue reference*. Lvalue references and rvalue references are distinct types. Except where explicitly noted, they are semantically equivalent and commonly referred to as references.

**3** [*Example 2*:

```cpp
void f(double& a) { a += 3.14; }
// ...
double d = 0;
f(d);
```

declares `a` to be a reference parameter of `f` so the call `f(d)` will add `3.14` to `d`.

```cpp
int v[20];
// ...
int& g(int i) { return v[i]; }
// ...
g(3) = 7;
```

declares the function `g()` to return a reference to an integer so `g(3)=7` will assign `7` to the fourth element of the array `v`. For another example,

```cpp
struct link {
  link* next;
};

link* first;

void h(link*& p) {  // p is a reference to pointer
  p->next = first;
  first = p;
  p = 0;
}

void k() {
   link* q = new link;
   h(q);
}
```

declares `p` to be a reference to a pointer to `link` so `h(q)` will leave `q` with the value zero. See also [[dcl.init.ref]](dcl.init.ref.md). — *end example*]

**4** It is unspecified whether or not a reference requires storage ([[basic.stc]](basic.stc.md)).

**5** There shall be no references to references, no arrays of references, and no pointers to references. The declaration of a reference shall contain an *initializer* ([[dcl.init.ref]](dcl.init.ref.md)) except when the declaration contains an explicit `extern` specifier ([[dcl.stc]](dcl.stc.md)), is a class member ([[class.mem]](class.mem.md)) declaration within a class definition, or is the declaration of a parameter or a return type ([[dcl.fct]](dcl.fct.md)); see [[basic.def]](basic.def.md). A reference shall be initialized to refer to a valid object or function. [*Note 2*: In particular, a null reference cannot exist in a well-defined program, because the only way to create such a reference would be to bind it to the “object” obtained by indirection through a null pointer, which causes undefined behavior. As described in [[class.bit]](class.bit.md), a reference cannot be bound directly to a bit-field. — *end note*]

**6** If a *typedef-name* ([[dcl.typedef]](dcl.typedef.md), [[temp.param]](temp.param.md)) or a *decltype-specifier* ([[dcl.type.simple]](dcl.type.simple.md)) denotes a type `TR` that is a reference to a type `T`, an attempt to create the type “lvalue reference to *cv* `TR`” creates the type “lvalue reference to `T`”, while an attempt to create the type “rvalue reference to *cv* `TR`” creates the type `TR`. [*Note 3*: This rule is known as reference collapsing. — *end note*] [*Example 3*:

```cpp
int i;
typedef int& LRI;
typedef int&& RRI;

LRI& r1 = i;                    // r1 has the type int&
const LRI& r2 = i;              // r2 has the type int&
const LRI&& r3 = i;             // r3 has the type int&

RRI& r4 = i;                    // r4 has the type int&
RRI&& r5 = 5;                   // r5 has the type int&&

decltype(r2)& r6 = i;           // r6 has the type int&
decltype(r2)&& r7 = i;          // r7 has the type int&
```

— *end example*]

**7** [*Note 4*: Forming a reference to function type is ill-formed if the function type has *cv-qualifier*s or a *ref-qualifier*; see [[dcl.fct]](dcl.fct.md). — *end note*]
