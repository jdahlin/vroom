# 13.8.4.2 Candidate functions [temp.dep.candidate]

[Contents](../README.md) · up: [13.8.4 Dependent name resolution \[temp.dep.res\]](temp.dep.res.md) · prev: [13.8.4.1 Point of instantiation \[temp.point\]](temp.point.md) · next: [13.8.5 Friend names declared within a class template \[temp.inject\]](temp.inject.md)

**1** For a function call where the *postfix-expression* is a dependent name, the candidate functions are found using the usual lookup rules from the template definition context ([[basic.lookup.unqual]](../basic/basic.lookup.unqual.md), [[basic.lookup.argdep]](../basic/basic.lookup.argdep.md)). [*Note 1*: For the part of the lookup using associated namespaces ([[basic.lookup.argdep]](../basic/basic.lookup.argdep.md)), function declarations found in the template instantiation context are found by this lookup, as described in [[basic.lookup.argdep]](../basic/basic.lookup.argdep.md). — *end note*] If the call would be ill-formed or would find a better match had the lookup within the associated namespaces considered all the function declarations with external linkage introduced in those namespaces in all translation units, not just considering those declarations found in the template definition and template instantiation contexts, then the program has undefined behavior.

**2** [*Example 1*:

*Source file `"X.h"`:*

```cpp
namespace Q {
  struct X { };
}
```

*Source file `"G.h"`:*

```cpp
namespace Q {
  void g_impl(X, X);
}
```

*Module interface unit of `M1`:*

```cpp
module;
#include "X.h"
#include "G.h"
export module M1;
export template<typename T>
void g(T t) {
  g_impl(t, Q::X{ });   // ADL in definition context finds Q::g_impl, g_impl not discarded
}
```

*Module interface unit of `M2`:*

```cpp
module;
#include "X.h"
export module M2;
import M1;
void h(Q::X x) {
   g(x);                // OK
}
```

— *end example*]

**3** [*Example 2*:

*Module interface unit of `Std`:*

```cpp
export module Std;
export template<typename Iter>
void indirect_swap(Iter lhs, Iter rhs)
{
  swap(*lhs, *rhs);     // swap not found by unqualified lookup, can be found only via ADL
}
```

*Module interface unit of `M`:*

```cpp
export module M;
import Std;

struct S { /* ...*/ };
void swap(S&, S&);      // #1

void f(S* p, S* q)
{
  indirect_swap(p, q);  // finds #1 via ADL in instantiation context
}
```

— *end example*]

**4** [*Example 3*:

*Source file `"X.h"`:*

```cpp
struct X { /* ... */ };
X operator+(X, X);
```

*Module interface unit of `F`:*

```cpp
export module F;
export template<typename T>
void f(T t) {
  t + t;
}
```

*Module interface unit of `M`:*

```cpp
module;
#include "X.h"
export module M;
import F;
void g(X x) {
  f(x);             // OK: instantiates f from F,
                    // operator+ is visible in instantiation context
}
```

— *end example*]

**5** [*Example 4*:

*Module interface unit of `A`:*

```cpp
export module A;
export template<typename T>
void f(T t) {
  cat(t, t);            // #1
  dog(t, t);            // #2
}
```

*Module interface unit of `B`:*

```cpp
export module B;
import A;
export template<typename T, typename U>
void g(T t, U u) {
  f(t);
}
```

*Source file `"foo.h"`, not an importable header:*

```cpp
struct foo {
  friend int cat(foo, foo);
};
int dog(foo, foo);
```

*Module interface unit of `C1`:*

```cpp
module;
#include "foo.h"        // dog not referenced, discarded
export module C1;
import B;
export template<typename T>
void h(T t) {
  g(foo{ }, t);
}
```

*Translation unit:*

```cpp
import C1;
void i() {
   h(0);                // error: dog not found at #2
}
```

*Importable header `"bar.h"`:*

```cpp
struct bar {
  friend int cat(bar, bar);
};
int dog(bar, bar);
```

*Module interface unit of `C2`:*

```cpp
module;
#include "bar.h"        // imports header unit "bar.h"
export module C2;
import B;
export template<typename T>
void j(T t) {
  g(bar{ }, t);
}
```

*Translation unit:*

```cpp
import C2;
void k() {
   j(0);                // OK, dog found in instantiation context:
                        // visible at end of module interface unit of C2
}
```

— *end example*]
