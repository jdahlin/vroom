# 9.8.2 Namespace alias [namespace.alias]

[Contents](../README.md) · up: [9.8 Namespaces \[basic.namespace\]](basic.namespace.md) · prev: [9.8.1.2 Namespace member definitions \[namespace.memdef\]](namespace.memdef.md) · next: [9.8.3 Using namespace directive \[namespace.udir\]](namespace.udir.md)

**1** A *namespace-alias-definition* declares an alternate name for a namespace according to the following grammar:

```bnf
namespace-alias:
        identifier
```

```bnf
namespace-alias-definition:
        'namespace' identifier '=' qualified-namespace-specifier ';'
```

```bnf
qualified-namespace-specifier:
    nested-name-specifierₒₚₜ namespace-name
```

**2** The *identifier* in a *namespace-alias-definition* is a synonym for the name of the namespace denoted by the *qualified-namespace-specifier* and becomes a *namespace-alias*. [*Note 1*: When looking up a *namespace-name* in a *namespace-alias-definition*, only namespace names are considered, see [[basic.lookup.udir]](../basic/basic.lookup.udir.md). — *end note*]

**3** In a declarative region, a *namespace-alias-definition* can be used to redefine a *namespace-alias* declared in that declarative region to refer only to the namespace to which it already refers. [*Example 1*: The following declarations are well-formed:

```cpp
namespace Company_with_very_long_name { /* ... */ }
namespace CWVLN = Company_with_very_long_name;
namespace CWVLN = Company_with_very_long_name;  // OK: duplicate
namespace CWVLN = CWVLN;
```

— *end example*]
