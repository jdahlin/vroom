# 9.8.1.1 Unnamed namespaces [namespace.unnamed]

[Contents](../README.md) · up: [9.8.1 Namespace definition \[namespace.def\]](namespace.def.md) · prev: [9.8.1 Namespace definition \[namespace.def\]](namespace.def.md) · next: [9.8.1.2 Namespace member definitions \[namespace.memdef\]](namespace.memdef.md)

**1** An *unnamed-namespace-definition* behaves as if it were replaced by

```bnf
'inline'ₒₚₜ 'namespace' unique '{' '/* empty body */' '}'
'using' 'namespace' unique ';'
'namespace' unique '{' namespace-body '}'
```

where `inline` appears if and only if it appears in the *unnamed-namespace-definition* and all occurrences of `unique` in a translation unit are replaced by the same identifier, and this identifier differs from all other identifiers in the translation unit. The optional *attribute-specifier-seq* in the *unnamed-namespace-definition* appertains to `unique`. [*Example 1*:

```cpp
namespace { int i; }            // unique::i
void f() { i++; }               // unique::i++

namespace A {
  namespace {
    int i;                      // A::unique::i
    int j;                      // A::unique::j
  }
  void g() { i++; }             // A::unique::i++
}

using namespace A;
void h() {
  i++;                          // error: unique::i or A::unique::i
  A::i++;                       // A::unique::i
  j++;                          // A::unique::j
}
```

— *end example*]
