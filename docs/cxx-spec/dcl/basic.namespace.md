# 9.8 Namespaces [basic.namespace]

[Contents](../README.md) · up: [9 Declarations \[dcl.dcl\]](dcl.dcl.md) · prev: [9.7.2 The `using enum` declaration \[enum.udecl\]](enum.udecl.md) · next: [9.8.1 Namespace definition \[namespace.def\]](namespace.def.md)

**1** A namespace is an optionally-named declarative region. The name of a namespace can be used to access entities declared in that namespace; that is, the members of the namespace. Unlike other declarative regions, the definition of a namespace can be split over several parts of one or more translation units.

**2** [*Note 1*: A namespace name with external linkage is exported if any of its *namespace-definition*s is exported, or if it contains any *export-declaration*s ([[module.interface]](../module/module.interface.md)). A namespace is never attached to a module, and never has module linkage even if it is not exported. — *end note*] [*Example 1*:

```cpp
export module M;
namespace N1 {}                 // N1 is not exported
export namespace N2 {}          // N2 is exported
namespace N3 { export int n; }  // N3 is exported
```

— *end example*]

**3** The outermost declarative region of a translation unit is a namespace; see [[basic.scope.namespace]](../basic/basic.scope.namespace.md).

## Subclauses

- [9.8.1 Namespace definition \[namespace.def\]](namespace.def.md)
  - [9.8.1.1 Unnamed namespaces \[namespace.unnamed\]](namespace.unnamed.md)
  - [9.8.1.2 Namespace member definitions \[namespace.memdef\]](namespace.memdef.md)
- [9.8.2 Namespace alias \[namespace.alias\]](namespace.alias.md)
- [9.8.3 Using namespace directive \[namespace.udir\]](namespace.udir.md)
