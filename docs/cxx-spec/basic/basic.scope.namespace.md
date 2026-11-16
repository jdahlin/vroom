# 6.4.6 Namespace scope [basic.scope.namespace]

[Contents](../README.md) · up: [6.4 Scope \[basic.scope\]](basic.scope.md) · prev: [6.4.5 Function scope \[basic.funscope\]](basic.funscope.md) · next: [6.4.7 Class scope \[basic.scope.class\]](basic.scope.class.md)

**1** The declarative region of a *namespace-definition* is its *namespace-body*. Entities declared in a *namespace-body* are said to be *members* of the namespace, and names introduced by these declarations into the declarative region of the namespace are said to be *member names* of the namespace. A namespace member name has namespace scope. Its potential scope includes its namespace from the name’s point of declaration ([[basic.scope.pdecl]](basic.scope.pdecl.md)) onwards; and for each *using-directive* ([[namespace.udir]](../dcl/namespace.udir.md)) that nominates the member’s namespace, the member’s potential scope includes that portion of the potential scope of the *using-directive* that follows the member’s point of declaration. [*Example 1*:

```cpp
namespace N {
  int i;
  int g(int a) { return a; }
  int j();
  void q();
}
namespace { int l=1; }
// the potential scope of l is from its point of declaration to the end of the translation unit

namespace N {
  int g(char a) {   // overloads N::g(int)
    return l+a;     // l is from unnamed namespace
  }

  int i;            // error: duplicate definition
  int j();          // OK: duplicate function declaration

  int j() {         // OK: definition of N::j()
    return g(i);    // calls N::g(int)
  }
  int q();          // error: different return type
}
```

— *end example*]

**2** If a translation unit *Q* is imported into a translation unit *R* ([[module.import]](../module/module.import.md)), the potential scope of a name *X* declared with namespace scope in *Q* is extended to include the portion of the corresponding namespace scope in *R* following the first *module-import-declaration* or *module-declaration* in *R* that imports *Q* (directly or indirectly) if

- *X* does not have internal linkage, and
- *X* is declared after the *module-declaration* in *Q* (if any), and
- either *X* is exported or *Q* and *R* are part of the same module.

[*Note 1*: A *module-import-declaration* imports both the named translation unit(s) and any modules named by exported *module-import-declaration*s within them, recursively. [*Example 2*:

*Translation unit #1:*

```cpp
export module Q;
export int sq(int i) { return i*i; }
```

*Translation unit #2:*

```cpp
export module R;
export import Q;
```

*Translation unit #3:*

```cpp
import R;
int main() { return sq(9); }    // OK: sq from module Q
```

— *end example*] — *end note*]

**3** A namespace member can also be referred to after the `::` scope resolution operator ([[expr.prim.id.qual]](../expr/expr.prim.id.qual.md)) applied to the name of its namespace or the name of a namespace which nominates the member’s namespace in a *using-directive*; see [[namespace.qual]](namespace.qual.md).

**4** The outermost declarative region of a translation unit is also a namespace, called the *global namespace*. A name declared in the global namespace has *global namespace scope* (also called *global scope*). The potential scope of such a name begins at its point of declaration ([[basic.scope.pdecl]](basic.scope.pdecl.md)) and ends at the end of the translation unit that is its declarative region. A name with global namespace scope is said to be a *global name*.
