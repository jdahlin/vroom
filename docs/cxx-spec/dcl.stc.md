# 9.2.1 Storage class specifiers [dcl.stc]

[Contents](README.md) · up: [9.2 Specifiers \[dcl.spec\]](dcl.spec.md) · prev: [9.2 Specifiers \[dcl.spec\]](dcl.spec.md) · next: [9.2.2 Function specifiers \[dcl.fct.spec\]](dcl.fct.spec.md)

**1** The storage class specifiers are

```bnf
storage-class-specifier:
    'static'
    'thread_local'
    'extern'
    'mutable'
```

At most one *storage-class-specifier* shall appear in a given *decl-specifier-seq*, except that `thread_local` may appear with `static` or `extern`. If `thread_local` appears in any declaration of a variable it shall be present in all declarations of that entity. If a *storage-class-specifier* appears in a *decl-specifier-seq*, there can be no `typedef` specifier in the same *decl-specifier-seq* and the *init-declarator-list* or *member-declarator-list* of the declaration shall not be empty (except for an anonymous union declared in a named namespace or in the global namespace, which shall be declared `static` ([[class.union.anon]](class.union.anon.md))). The *storage-class-specifier* applies to the name declared by each *init-declarator* in the list and not to any names declared by other specifiers. [*Note 1*: See [[temp.expl.spec]](temp.expl.spec.md) and [[temp.explicit]](temp.explicit.md) for restrictions in explicit specializations and explicit instantiations, respectively. — *end note*]

**2** [*Note 2*: A variable declared without a *storage-class-specifier* at block scope or declared as a function parameter has automatic storage duration by default ([[basic.stc.auto]](basic.stc.auto.md)). — *end note*]

**3** The `thread_local` specifier indicates that the named entity has thread storage duration ([[basic.stc.thread]](basic.stc.thread.md)). It shall be applied only to the declaration of a variable of namespace or block scope, to a structured binding declaration ([[dcl.struct.bind]](dcl.struct.bind.md)), or to the declaration of a static data member. When `thread_local` is applied to a variable of block scope the *storage-class-specifier* `static` is implied if no other *storage-class-specifier* appears in the *decl-specifier-seq*.

**4** The `static` specifier shall be applied only to the declaration of a variable or function, to a structured binding declaration ([[dcl.struct.bind]](dcl.struct.bind.md)), or to the declaration of an anonymous union ([[class.union.anon]](class.union.anon.md)). There can be no `static` function declarations within a block, nor any `static` function parameters. A `static` specifier used in the declaration of a variable declares the variable to have static storage duration ([[basic.stc.static]](basic.stc.static.md)), unless accompanied by the `thread_local` specifier, which declares the variable to have thread storage duration ([[basic.stc.thread]](basic.stc.thread.md)). A `static` specifier can be used in declarations of class members; [[class.static]](class.static.md) describes its effect. For the linkage of a name declared with a `static` specifier, see [[basic.link]](basic.link.md).

**5** The `extern` specifier shall be applied only to the declaration of a variable or function. The `extern` specifier shall not be used in the declaration of a class member or function parameter. For the linkage of a name declared with an `extern` specifier, see [[basic.link]](basic.link.md). [*Note 3*: The `extern` keyword can also be used in *explicit-instantiation*s and *linkage-specification*s, but it is not a *storage-class-specifier* in such contexts. — *end note*]

**6** The linkages implied by successive declarations for a given entity shall agree. That is, within a given scope, each declaration declaring the same variable name or the same overloading of a function name shall imply the same linkage. [*Example 1*:

```cpp
static char* f();               // f() has internal linkage
char* f()                       // f() still has internal linkage
  { /* ... */ }

char* g();                      // g() has external linkage
static char* g()                // error: inconsistent linkage
  { /* ... */ }

void h();
inline void h();                // external linkage

inline void l();
void l();                       // external linkage

inline void m();
extern void m();                // external linkage

static void n();
inline void n();                // internal linkage

static int a;                   // a has internal linkage
int a;                          // error: two definitions

static int b;                   // b has internal linkage
extern int b;                   // b still has internal linkage

int c;                          // c has external linkage
static int c;                   // error: inconsistent linkage

extern int d;                   // d has external linkage
static int d;                   // error: inconsistent linkage
```

— *end example*]

**7** The name of a declared but undefined class can be used in an `extern` declaration. Such a declaration can only be used in ways that do not require a complete class type. [*Example 2*:

```cpp
struct S;
extern S a;
extern S f();
extern void g(S);

void h() {
  g(a);                         // error: S is incomplete
  f();                          // error: S is incomplete
}
```

— *end example*]

**8** The `mutable` specifier shall appear only in the declaration of a non-static data member ([[class.mem]](class.mem.md)) whose type is neither const-qualified nor a reference type. [*Example 3*:

```cpp
class X {
  mutable const int* p;         // OK
  mutable int* const q;         // error
};
```

— *end example*]

**9** [*Note 4*: The `mutable` specifier on a class data member nullifies a `const` specifier applied to the containing class object and permits modification of the mutable class member even though the rest of the object is const ([[basic.type.qualifier]](basic.type.qualifier.md), [[dcl.type.cv]](dcl.type.cv.md)). — *end note*]
