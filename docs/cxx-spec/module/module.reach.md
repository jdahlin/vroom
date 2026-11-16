# 10.7 Reachability [module.reach]

[Contents](../README.md) · up: [10 Modules \[module\]](module.md) · prev: [10.6 Instantiation context \[module.context\]](module.context.md) · next: [11 Classes \[class\]](../class/class.md)

**1** A translation unit *U* is *necessarily reachable* from a point *P* if *U* is a module interface unit on which the translation unit containing *P* has an interface dependency, or the translation unit containing *P* imports *U*, in either case prior to *P* ([[module.import]](module.import.md)). [*Note 1*: While module interface units are reachable even when they are only transitively imported via a non-exported import declaration, namespace-scope names from such module interface units are not visible to name lookup ([[basic.scope.namespace]](../basic/basic.scope.namespace.md)). — *end note*]

**2** All translation units that are necessarily reachable are *reachable*. It is unspecified whether additional translation units on which the point within the program has an interface dependency are considered reachable, and under what circumstances.[^1] [*Note 2*: It is advisable to avoid depending on the reachability of any additional translation units in programs intending to be portable. — *end note*]

**3** A declaration *D* is *reachable* if, for any point *P* in the instantiation context ([[module.context]](module.context.md)),

- *D* appears prior to *P* in the same translation unit, or
- *D* is not discarded ([[module.global.frag]](module.global.frag.md)), appears in a translation unit that is reachable from *P*, and does not appear within a *private-module-fragment*.

[*Note 3*: Whether a declaration is exported has no bearing on whether it is reachable. — *end note*]

**4** The accumulated properties of all reachable declarations of an entity within a context determine the behavior of the entity within that context. [*Note 4*: These reachable semantic properties include type completeness, type definitions, initializers, default arguments of functions or template declarations, attributes, visibility of class or enumeration member names to ordinary lookup, etc. Since default arguments are evaluated in the context of the call expression, the reachable semantic properties of the corresponding parameter types apply in that context. [*Example 1*:

*Translation unit #1:*

```cpp
export module M:A;
export struct B;
```

*Translation unit #2:*

```cpp
module M:B;
struct B {
  operator int();
};
```

*Translation unit #3:*

```cpp
module M:C;
import :A;
B b1;                           // error: no reachable definition of struct B
```

*Translation unit #4:*

```cpp
export module M;
export import :A;
import :B;
B b2;
export void f(B b = B());
```

*Translation unit #5:*

```cpp
module X;
import M;
B b3;                           // error: no reachable definition of struct B
void g() { f(); }               // error: no reachable definition of struct B
```

— *end example*] — *end note*]

**5** [*Note 5*: An entity can have reachable declarations even if it is not visible to name lookup. — *end note*] [*Example 2*:

*Translation unit #1:*

```cpp
export module A;
struct X {};
export using Y = X;
```

*Translation unit #2:*

```cpp
module B;
import A;
Y y;                // OK, definition of X is reachable
X x;                // error: X not visible to unqualified lookup
```

— *end example*]

[^1]: Implementations are therefore not required to prevent the semantic effects of additional translation units involved in the compilation from being observed.
