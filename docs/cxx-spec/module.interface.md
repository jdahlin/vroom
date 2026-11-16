# 10.2 Export declaration [module.interface]

[Contents](README.md) · up: [10 Modules \[module\]](module.md) · prev: [10.1 Module units and purviews \[module.unit\]](module.unit.md) · next: [10.3 Import declaration \[module.import\]](module.import.md)

```bnf
export-declaration:
    'export' declaration
    'export' '{' declaration-seqₒₚₜ '}'
    export-keyword module-import-declaration
```

**1** An *export-declaration* shall appear only at namespace scope and only in the purview of a module interface unit. An *export-declaration* shall not appear directly or indirectly within an unnamed namespace or a *private-module-fragment*. An *export-declaration* has the declarative effects of its *declaration*, *declaration-seq* (if any), or *module-import-declaration*. An *export-declaration* does not establish a scope and its *declaration* or *declaration-seq* shall not contain an *export-declaration* or *module-import-declaration*.

**2** A declaration is *exported* if it is

- a namespace-scope declaration declared within an *export-declaration*, or
- a *namespace-definition* that contains an exported declaration, or
- a declaration within a header unit ([[module.import]](module.import.md)) that introduces at least one name.

**3** An exported declaration that is not a *module-import-declaration* shall declare at least one name. If the declaration is not within a header unit, it shall not declare a name with internal linkage.

**4** [*Example 1*:

*Source file `"a.h"`:*

```cpp
export int x;
```

*Translation unit #1:*

```cpp
module;
#include "a.h"                  // error: declaration of x is not in the
                                // purview of a module interface unit
export module M;
export namespace {}             // error: does not introduce any names
export namespace {
  int a1;                       // error: export of name with internal linkage
}
namespace {
  export int a2;                // error: export of name with internal linkage
}
export static int b;            // error: b explicitly declared static
export int f();                 // OK
export namespace N { }          // OK
export using namespace N;       // error: does not declare a name
```

— *end example*]

**5** If the declaration is a *using-declaration* ([[namespace.udecl]](namespace.udecl.md)) and is not within a header unit, all entities to which all of the *using-declarator*s ultimately refer (if any) shall have been introduced with a name having external linkage. [*Example 2*:

*Source file `"b.h"`:*

```cpp
int f();
```

*Importable header `"c.h"`:*

```cpp
int g();
```

*Translation unit #1:*

```cpp
export module X;
export int h();
```

*Translation unit #2:*

```cpp
module;
#include "b.h"
export module M;
import "c.h";
import X;
export using ::f, ::g, ::h;     // OK
struct S;
export using ::S;               // error: S has module linkage
namespace N {
  export int h();
  static int h(int);            // #1
}
export using N::h;              // error: #1 has internal linkage
```

— *end example*] [*Note 1*: These constraints do not apply to type names introduced by `typedef` declarations and *alias-declaration*s. [*Example 3*:

```cpp
export module M;
struct S;
export using T = S;             // OK, exports name T denoting type S
```

— *end example*] — *end note*]

**6** A redeclaration of an exported declaration of an entity is implicitly exported. An exported redeclaration of a non-exported declaration of an entity is ill-formed. [*Example 4*:

```cpp
export module M;
struct S { int n; };
typedef S S;
export typedef S S;             // OK, does not redeclare an entity
export struct S;                // error: exported declaration follows non-exported declaration
```

— *end example*]

**7** A name is *exported* by a module if it is introduced or redeclared by an exported declaration in the purview of that module. [*Note 2*: Exported names have either external linkage or no linkage; see [[basic.link]](basic.link.md). Namespace-scope names exported by a module are visible to name lookup in any translation unit importing that module; see [[basic.scope.namespace]](basic.scope.namespace.md). Class and enumeration member names are visible to name lookup in any context in which a definition of the type is reachable. — *end note*] [*Example 5*:

*Interface unit of `M`:*

```cpp
export module M;
export struct X {
  static void f();
  struct Y { };
};

namespace {
  struct S { };
}
export void f(S);               // OK
struct T { };
export T id(T);                 // OK

export struct A;                // A exported as incomplete

export auto rootFinder(double a) {
  return [=](double x) { return (x + a/x)/2; };
}

export const int n = 5;         // OK, n has external linkage
```

*Implementation unit of `M`:*

```cpp
module M;
struct A {
  int value;
};
```

*Main program:*

```cpp
import M;
int main() {
  X::f();                       // OK, X is exported and definition of X is reachable
  X::Y y;                       // OK, X::Y is exported as a complete type
  auto f = rootFinder(2);       // OK
  return A{45}.value;           // error: A is incomplete
}
```

— *end example*]

**8** [*Note 3*: Redeclaring a name in an *export-declaration* cannot change the linkage of the name ([[basic.link]](basic.link.md)). [*Example 6*:

*Interface unit of `M`:*

```cpp
export module M;
static int f();                 // #1
export int f();                 // error: #1 gives internal linkage
struct S;                       // #2
export struct S;                // error: #2 gives module linkage
namespace {
  namespace N {
    extern int x;               // #3
  }
}
export int N::x;                // error: #3 gives internal linkage
```

— *end example*] — *end note*]

**9** [*Note 4*: Declarations in an exported *namespace-definition* or in an exported *linkage-specification* ([[dcl.link]](dcl.link.md)) are exported and subject to the rules of exported declarations. [*Example 7*:

```cpp
export module M;
export namespace N {
  int x;                        // OK
  static_assert(1 == 1);        // error: does not declare a name
}
```

— *end example*] — *end note*]
