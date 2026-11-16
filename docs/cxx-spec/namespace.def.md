# 9.8.1 Namespace definition [namespace.def]

[Contents](README.md) · up: [9.8 Namespaces \[basic.namespace\]](basic.namespace.md) · prev: [9.8 Namespaces \[basic.namespace\]](basic.namespace.md) · next: [9.8.1.1 Unnamed namespaces \[namespace.unnamed\]](namespace.unnamed.md)

```bnf
namespace-name:
        identifier
        namespace-alias
```

```bnf
namespace-definition:
        named-namespace-definition
        unnamed-namespace-definition
        nested-namespace-definition
```

```bnf
named-namespace-definition:
        'inline'ₒₚₜ 'namespace' attribute-specifier-seqₒₚₜ identifier '{' namespace-body '}'
```

```bnf
unnamed-namespace-definition:
        'inline'ₒₚₜ 'namespace' attribute-specifier-seqₒₚₜ '{' namespace-body '}'
```

```bnf
nested-namespace-definition:
        'namespace' enclosing-namespace-specifier '::' 'inline'ₒₚₜ identifier '{' namespace-body '}'
```

```bnf
enclosing-namespace-specifier:
        identifier
        enclosing-namespace-specifier '::' 'inline'ₒₚₜ identifier
```

```bnf
namespace-body:
        declaration-seqₒₚₜ
```

**1** Every *namespace-definition* shall appear at namespace scope ([[basic.scope.namespace]](basic.scope.namespace.md)).

**2** In a *named-namespace-definition*, the *identifier* is the name of the namespace. If the *identifier*, when looked up ([[basic.lookup.unqual]](basic.lookup.unqual.md)), refers to a *namespace-name* (but not a *namespace-alias*) that was introduced in the namespace in which the *named-namespace-definition* appears or that was introduced in a member of the inline namespace set of that namespace, the *namespace-definition* *extends* the previously-declared namespace. Otherwise, the *identifier* is introduced as a *namespace-name* into the declarative region in which the *named-namespace-definition* appears.

**3** Because a *namespace-definition* contains *declaration*s in its *namespace-body* and a *namespace-definition* is itself a *declaration*, it follows that *namespace-definition*s can be nested. [*Example 1*:

```cpp
namespace Outer {
  int i;
  namespace Inner {
    void f() { i++; }           // Outer::i
    int i;
    void g() { i++; }           // Inner::i
  }
}
```

— *end example*]

**4** The *enclosing namespaces* of a declaration are those namespaces in which the declaration lexically appears, except for a redeclaration of a namespace member outside its original namespace (e.g., a definition as specified in [[namespace.memdef]](namespace.memdef.md)). Such a redeclaration has the same enclosing namespaces as the original declaration. [*Example 2*:

```cpp
namespace Q {
  namespace V {
    void f();                   // enclosing namespaces are the global namespace, Q, and Q::V
    class C { void m(); };
  }
  void V::f() {                 // enclosing namespaces are the global namespace, Q, and Q::V
    extern void h();            // ... so this declares Q::V::h
  }
  void V::C::m() {              // enclosing namespaces are the global namespace, Q, and Q::V
  }
}
```

— *end example*]

**5** If the optional initial `inline` keyword appears in a *namespace-definition* for a particular namespace, that namespace is declared to be an *inline namespace*. The `inline` keyword may be used on a *namespace-definition* that extends a namespace only if it was previously used on the *namespace-definition* that initially declared the *namespace-name* for that namespace.

**6** The optional *attribute-specifier-seq* in a *named-namespace-definition* appertains to the namespace being defined or extended.

**7** Members of an inline namespace can be used in most respects as though they were members of the enclosing namespace. Specifically, the inline namespace and its enclosing namespace are both added to the set of associated namespaces used in argument-dependent lookup ([[basic.lookup.argdep]](basic.lookup.argdep.md)) whenever one of them is, and a *using-directive* ([[namespace.udir]](namespace.udir.md)) that names the inline namespace is implicitly inserted into the enclosing namespace as for an unnamed namespace ([[namespace.unnamed]](namespace.unnamed.md)). Furthermore, each member of the inline namespace can subsequently be partially specialized ([[temp.class.spec]](temp.class.spec.md)), explicitly instantiated ([[temp.explicit]](temp.explicit.md)), or explicitly specialized ([[temp.expl.spec]](temp.expl.spec.md)) as though it were a member of the enclosing namespace. Finally, looking up a name in the enclosing namespace via explicit qualification ([[namespace.qual]](namespace.qual.md)) will include members of the inline namespace brought in by the *using-directive* even if there are declarations of that name in the enclosing namespace.

**8** These properties are transitive: if a namespace `N` contains an inline namespace `M`, which in turn contains an inline namespace `O`, then the members of `O` can be used as though they were members of `M` or `N`. The *inline namespace set* of `N` is the transitive closure of all inline namespaces in `N`. The *enclosing namespace set* of `O` is the set of namespaces consisting of the innermost non-inline namespace enclosing an inline namespace `O`, together with any intervening inline namespaces.

**9** A *nested-namespace-definition* with an *enclosing-namespace-specifier* `E`, *identifier* `I` and *namespace-body* `B` is equivalent to

```cpp
namespace E { inlineₒₚₜ namespace I { B } }
```

where the optional `inline` is present if and only if the *identifier* `I` is preceded by `inline`. [*Example 3*:

```cpp
namespace A::inline B::C {
  int i;
}
```

The above has the same effect as:

```cpp
namespace A {
  inline namespace B {
    namespace C {
      int i;
    }
  }
}
```

— *end example*]

## Subclauses

- [9.8.1.1 Unnamed namespaces \[namespace.unnamed\]](namespace.unnamed.md)
- [9.8.1.2 Namespace member definitions \[namespace.memdef\]](namespace.memdef.md)
