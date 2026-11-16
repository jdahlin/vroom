# 9.2.8.1 The *cv-qualifier*s [dcl.type.cv]

[Contents](README.md) · up: [9.2.8 Type specifiers \[dcl.type\]](dcl.type.md) · prev: [9.2.8 Type specifiers \[dcl.type\]](dcl.type.md) · next: [9.2.8.2 Simple type specifiers \[dcl.type.simple\]](dcl.type.simple.md)

**1** There are two *cv-qualifier*s, `const` and `volatile`. Each *cv-qualifier* shall appear at most once in a *cv-qualifier-seq*. If a *cv-qualifier* appears in a *decl-specifier-seq*, the *init-declarator-list* or *member-declarator-list* of the declaration shall not be empty. [*Note 1*: [[basic.type.qualifier]](basic.type.qualifier.md) and [[dcl.fct]](dcl.fct.md) describe how cv-qualifiers affect object and function types. — *end note*] Redundant cv-qualifications are ignored. [*Note 2*: For example, these could be introduced by typedefs. — *end note*]

**2** [*Note 3*: Declaring a variable `const` can affect its linkage ([[dcl.stc]](dcl.stc.md)) and its usability in constant expressions ([[expr.const]](expr.const.md)). As described in [[dcl.init]](dcl.init.md), the definition of an object or subobject of const-qualified type must specify an initializer or be subject to default-initialization. — *end note*]

**3** A pointer or reference to a cv-qualified type need not actually point or refer to a cv-qualified object, but it is treated as if it does; a const-qualified access path cannot be used to modify an object even if the object referenced is a non-const object and can be modified through some other access path. [*Note 4*: Cv-qualifiers are supported by the type system so that they cannot be subverted without casting ([[expr.const.cast]](expr.const.cast.md)). — *end note*]

**4** Any attempt to modify ([[expr.ass]](expr.ass.md), [[expr.post.incr]](expr.post.incr.md), [[expr.pre.incr]](expr.pre.incr.md)) a const object ([[basic.type.qualifier]](basic.type.qualifier.md)) during its lifetime ([[basic.life]](basic.life.md)) results in undefined behavior. [*Example 1*:

```cpp
const int ci = 3;                       // cv-qualified (initialized as required)
ci = 4;                                 // error: attempt to modify const

int i = 2;                              // not cv-qualified
const int* cip;                         // pointer to const int
cip = &i;                               // OK: cv-qualified access path to unqualified
*cip = 4;                               // error: attempt to modify through ptr to const

int* ip;
ip = const_cast<int*>(cip);             // cast needed to convert const int* to int*
*ip = 4;                                // defined: *ip points to i, a non-const object

const int* ciq = new const int (3);     // initialized as required
int* iq = const_cast<int*>(ciq);        // cast required
*iq = 4;                                // undefined behavior: modifies a const object
```

For another example,

```cpp
struct X {
  mutable int i;
  int j;
};
struct Y {
  X x;
  Y();
};

const Y y;
y.x.i++;                                // well-formed: mutable member can be modified
y.x.j++;                                // error: const-qualified member modified
Y* p = const_cast<Y*>(&y);              // cast away const-ness of y
p->x.i = 99;                            // well-formed: mutable member can be modified
p->x.j = 99;                            // undefined behavior: modifies a const subobject
```

— *end example*]

**5** The semantics of an access through a volatile glvalue are implementation-defined. If an attempt is made to access an object defined with a volatile-qualified type through the use of a non-volatile glvalue, the behavior is undefined.

**6** [*Note 5*: `volatile` is a hint to the implementation to avoid aggressive optimization involving the object because the value of the object might be changed by means undetectable by an implementation. Furthermore, for some implementations, `volatile` might indicate that special hardware instructions are required to access the object. See [[intro.execution]](intro.execution.md) for detailed semantics. In general, the semantics of `volatile` are intended to be the same in C++ as they are in C. — *end note*]
