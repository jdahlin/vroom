# 6.6 Program and linkage [basic.link]

[Contents](README.md) · up: [6 Basics \[basic\]](basic.md) · prev: [6.5.6 Using-directives and namespace aliases \[basic.lookup.udir\]](basic.lookup.udir.md) · next: [6.7 Memory and objects \[basic.memobj\]](basic.memobj.md)

**1** A *program* consists of one or more translation units ([[lex.separate]](lex.separate.md)) linked together. A translation unit consists of a sequence of declarations.

```bnf
translation-unit:
    declaration-seqₒₚₜ
    global-module-fragmentₒₚₜ module-declaration declaration-seqₒₚₜ private-module-fragmentₒₚₜ
```

**2** A name is said to have *linkage* when it might denote the same object, reference, function, type, template, namespace or value as a name introduced by a declaration in another scope:

- When a name has *external linkage*, the entity it denotes can be referred to by names from scopes of other translation units or from other scopes of the same translation unit.
- When a name has *module linkage*, the entity it denotes can be referred to by names from other scopes of the same module unit ([[module.unit]](module.unit.md)) or from scopes of other module units of that same module.
- When a name has *internal linkage*, the entity it denotes can be referred to by names from other scopes in the same translation unit.
- When a name has *no linkage*, the entity it denotes cannot be referred to by names from other scopes.

**3** A name having namespace scope ([[basic.scope.namespace]](basic.scope.namespace.md)) has internal linkage if it is the name of

- a variable, variable template, function, or function template that is explicitly declared `static`; or

- a non-template variable of non-volatile const-qualified type, unless

  - it is explicitly declared `extern`, or
  - it is inline or exported, or
  - it was previously declared and the prior declaration did not have internal linkage; or

- a data member of an anonymous union.

[*Note 1*: An instantiated variable template that has const-qualified type can have external or module linkage, even if not declared `extern`. — *end note*]

**4** An unnamed namespace or a namespace declared directly or indirectly within an unnamed namespace has internal linkage. All other namespaces have external linkage. A name having namespace scope that has not been given internal linkage above and that is the name of

- a variable; or
- a function; or
- a named class ([[class.pre]](class.pre.md)), or an unnamed class defined in a typedef declaration in which the class has the typedef name for linkage purposes ([[dcl.typedef]](dcl.typedef.md)); or
- a named enumeration ([[dcl.enum]](dcl.enum.md)), or an unnamed enumeration defined in a typedef declaration in which the enumeration has the typedef name for linkage purposes ([[dcl.typedef]](dcl.typedef.md)); or
- an unnamed enumeration that has an enumerator as a name for linkage purposes ([[dcl.enum]](dcl.enum.md)); or
- a template

has its linkage determined as follows:

- if the enclosing namespace has internal linkage, the name has internal linkage;
- otherwise, if the declaration of the name is attached to a named module ([[module.unit]](module.unit.md)) and is not exported ([[module.interface]](module.interface.md)), the name has module linkage;
- otherwise, the name has external linkage.

**5** In addition, a member function, static data member, a named class or enumeration of class scope, or an unnamed class or enumeration defined in a class-scope typedef declaration such that the class or enumeration has the typedef name for linkage purposes ([[dcl.typedef]](dcl.typedef.md)), has the same linkage, if any, as the name of the class of which it is a member.

**6** The name of a function declared in block scope and the name of a variable declared by a block scope `extern` declaration have linkage. If such a declaration is attached to a named module, the program is ill-formed. If there is a visible declaration of an entity with linkage, ignoring entities declared outside the innermost enclosing namespace scope, such that the block scope declaration would be a (possibly ill-formed) redeclaration if the two declarations appeared in the same declarative region, the block scope declaration declares that same entity and receives the linkage of the previous declaration. If there is more than one such matching entity, the program is ill-formed. Otherwise, if no matching entity is found, the block scope entity receives external linkage. If, within a translation unit, the same entity is declared with both internal and external linkage, the program is ill-formed. [*Example 1*:

```cpp
static void f();
extern "C" void h();
static int i = 0;               // #1
void g() {
  extern void f();              // internal linkage
  extern void h();              // C language linkage
  int i;                        // #2: i has no linkage
  {
    extern void f();            // internal linkage
    extern int i;               // #3: external linkage, ill-formed
  }
}
```

Without the declaration at line #2, the declaration at line #3 would link with the declaration at line #1. Because the declaration with internal linkage is hidden, however, #3 is given external linkage, making the program ill-formed. — *end example*]

**7** When a block scope declaration of an entity with linkage is not found to refer to some other declaration, then that entity is a member of the innermost enclosing namespace. However such a declaration does not introduce the member name in its namespace scope. [*Example 2*:

```cpp
namespace X {
  void p() {
    q();                        // error: q not yet declared
    extern void q();            // q is a member of namespace X
  }

  void middle() {
    q();                        // error: q not yet declared
  }

  void q() { /* ... */ }        // definition of X::q
}

void q() { /* ... */ }          // some other, unrelated q
```

— *end example*]

**8** Names not covered by these rules have no linkage. Moreover, except as noted, a name declared at block scope ([[basic.scope.block]](basic.scope.block.md)) has no linkage.

**9** Two names that are the same ([[basic.pre]](basic.pre.md)) and that are declared in different scopes shall denote the same variable, function, type, template or namespace if

- both names have external or module linkage and are declared in declarations attached to the same module, or else both names have internal linkage and are declared in the same translation unit; and
- both names refer to members of the same namespace or to members, not by inheritance, of the same class; and
- when both names denote functions or function templates, the signatures ([[defns.signature]](intro.defs.md#defns.signature), [[defns.signature.templ]](intro.defs.md#defns.signature.templ)) are the same.

If multiple declarations of the same name with external linkage would declare the same entity except that they are attached to different modules, the program is ill-formed; no diagnostic is required. [*Note 2*: *using-declaration*s, typedef declarations, and *alias-declaration*s do not declare entities, but merely introduce synonyms. Similarly, *using-directive*s do not declare entities. Enumerators do not have linkage, but may serve as the name of an enumeration with linkage ([[dcl.enum]](dcl.enum.md)). — *end note*]

**10** If a declaration would redeclare a reachable declaration attached to a different module, the program is ill-formed. [*Example 3*:

*`"decls.h"`:*

```cpp
int f();            // #1, attached to the global module
int g();            // #2, attached to the global module
```

*Module interface of `M`:*

```cpp
module;
#include "decls.h"
export module M;
export using ::f;   // OK: does not declare an entity, exports #1
int g();            // error: matches #2, but attached to M
export int h();     // #3
export int k();     // #4
```

*Other translation unit:*

```cpp
import M;
static int h();     // error: matches #3
int k();            // error: matches #4
```

— *end example*] As a consequence of these rules, all declarations of an entity are attached to the same module; the entity is said to be *attached* to that module.

**11** After all adjustments of types (during which typedefs ([[dcl.typedef]](dcl.typedef.md)) are replaced by their definitions), the types specified by all declarations referring to a given variable or function shall be identical, except that declarations for an array object can specify array types that differ by the presence or absence of a major array bound ([[dcl.array]](dcl.array.md)). A violation of this rule on type identity does not require a diagnostic.

**12** [*Note 3*: Linkage to non-C++ declarations can be achieved using a *linkage-specification* ([[dcl.link]](dcl.link.md)). — *end note*]

**13** A declaration *D* *names* an entity *E* if

- *D* contains a *lambda-expression* whose closure type is *E*,
- *E* is not a function or function template and *D* contains an *id-expression*, *type-specifier*, *nested-name-specifier*, *template-name*, or *concept-name* denoting *E*, or
- *E* is a function or function template and *D* contains an expression that names *E* ([[basic.def.odr]](basic.def.odr.md)) or an *id-expression* that refers to a set of overloads that contains *E*. [*Note 4*: Non-dependent names in an instantiated declaration do not refer to a set of overloads ([[temp.nondep]](temp.nondep.md)). — *end note*]

**14** A declaration is an *exposure* if it either names a TU-local entity (defined below), ignoring

- the *function-body* for a non-inline function or function template (but not the deduced return type for a (possibly instantiated) definition of a function with a declared return type that uses a placeholder type ([[dcl.spec.auto]](dcl.spec.auto.md))),
- the *initializer* for a variable or variable template (but not the variable’s type),
- friend declarations in a class definition, and
- any reference to a non-volatile const object or reference with internal or no linkage initialized with a constant expression that is not an odr-use ([[basic.def.odr]](basic.def.odr.md)),

or defines a constexpr variable initialized to a TU-local value (defined below). [*Note 5*: An inline function template can be an exposure even though explicit specializations of it might be usable in other translation units. — *end note*]

**15** An entity is *TU-local* if it is

- a type, function, variable, or template that

  - has a name with internal linkage, or
  - does not have a name with linkage and is declared, or introduced by a *lambda-expression*, within the definition of a TU-local entity,

- a type with no name that is defined outside a *class-specifier*, function body, or *initializer* or is introduced by a *defining-type-specifier* that is used to declare only TU-local entities,

- a specialization of a TU-local template,

- a specialization of a template with any TU-local template argument, or

- a specialization of a template whose (possibly instantiated) declaration is an exposure. [*Note 6*: The specialization might have been implicitly or explicitly instantiated. — *end note*]

**16** A value or object is *TU-local* if either

- it is, or is a pointer to, a TU-local function or the object associated with a TU-local variable,
- it is an object of class or array type and any of its subobjects or any of the objects or functions to which its non-static data members of reference type refer is TU-local and is usable in constant expressions.

**17** If a (possibly instantiated) declaration of, or a deduction guide for, a non-TU-local entity in a module interface unit (outside the *private-module-fragment*, if any) or module partition ([[module.unit]](module.unit.md)) is an exposure, the program is ill-formed. Such a declaration in any other context is deprecated ([[depr.local]](depr.local.md)).

**18** If a declaration that appears in one translation unit names a TU-local entity declared in another translation unit that is not a header unit, the program is ill-formed. A declaration instantiated for a template specialization ([[temp.spec]](temp.spec.md)) appears at the point of instantiation of the specialization ([[temp.point]](temp.point.md)).

**19** [*Example 4*:

*Translation unit #1:*

```cpp
export module A;
static void f() {}
inline void it() { f(); }           // error: is an exposure of f
static inline void its() { f(); }   // OK
template<int> void g() { its(); }   // OK
template void g<0>();

decltype(f) *fp;                    // error: f (though not its type) is TU-local
auto &fr = f;                       // OK
constexpr auto &fr2 = fr;           // error: is an exposure of f
constexpr static auto fp2 = fr;     // OK

struct S { void (&ref)(); } s{f};               // OK, value is TU-local
constexpr extern struct W { S &s; } wrap{s};    // OK, value is not TU-local

static auto x = []{f();};           // OK
auto x2 = x;                        // error: the closure type is TU-local
int y = ([]{f();}(),0);             // error: the closure type is not TU-local
int y2 = (x,0);                     // OK

namespace N {
  struct A {};
  void adl(A);
  static void adl(int);
}
void adl(double);

inline void h(auto x) { adl(x); }   // OK, but a specialization might be an exposure
```

*Translation unit #2:*

```cpp
module A;
void other() {
  g<0>();                           // OK, specialization is explicitly instantiated
  g<1>();                           // error: instantiation uses TU-local its
  h(N::A{});                        // error: overload set contains TU-local N::adl(int)
  h(0);                             // OK, calls adl(double)
  adl(N::A{});                      // OK; N::adl(int) not found, calls N::adl(N::A)
  fr();                             // OK, calls f
  constexpr auto ptr = fr;          // error: fr is not usable in constant expressions here
}
```

— *end example*]
