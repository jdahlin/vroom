# 6.5 Name lookup [basic.lookup]

[Contents](../README.md) · up: [6 Basics \[basic\]](basic.md) · prev: [6.4.10 Name hiding \[basic.scope.hiding\]](basic.scope.hiding.md) · next: [6.5.1 Unqualified name lookup \[basic.lookup.unqual\]](basic.lookup.unqual.md)

**1** The name lookup rules apply uniformly to all names (including *typedef-name*s ([[dcl.typedef]](../dcl/dcl.typedef.md)), *namespace-name*s ([[basic.namespace]](../dcl/basic.namespace.md)), and *class-name*s ([[class.name]](../class/class.name.md))) wherever the grammar allows such names in the context discussed by a particular rule. Name lookup associates the use of a name with a set of declarations ([[basic.def]](basic.def.md)) of that name. If the declarations found by name lookup all denote functions or function templates, the declarations are said to form an *overload set*. The declarations found by name lookup shall either all denote the same entity or form an overload set. Overload resolution ([[over.match]](../over/over.match.md), [[over.over]](../over/over.over.md)) takes place after name lookup has succeeded. The access rules ([[class.access]](../class/class.access.md)) are considered only once name lookup and function overload resolution (if applicable) have succeeded. Only after name lookup, function overload resolution (if applicable) and access checking have succeeded are the semantic properties introduced by the name’s declaration and its reachable ([[module.reach]](../module/module.reach.md)) redeclarations used further in expression processing ([[expr]](../expr/expr.md)).

**2** A name “looked up in the context of an expression” is looked up in the scope where the expression is found.

**3** The injected-class-name of a class ([[class.pre]](../class/class.pre.md)) is also considered to be a member of that class for the purposes of name hiding and lookup.

**4** [*Note 1*: [[basic.link]](basic.link.md) discusses linkage issues. The notions of scope, point of declaration and name hiding are discussed in [[basic.scope]](basic.scope.md). — *end note*]

## Subclauses

- [6.5.1 Unqualified name lookup \[basic.lookup.unqual\]](basic.lookup.unqual.md)
- [6.5.2 Argument-dependent name lookup \[basic.lookup.argdep\]](basic.lookup.argdep.md)
- [6.5.3 Qualified name lookup \[basic.lookup.qual\]](basic.lookup.qual.md)
  - [6.5.3.1 Class members \[class.qual\]](class.qual.md)
  - [6.5.3.2 Namespace members \[namespace.qual\]](namespace.qual.md)
- [6.5.4 Elaborated type specifiers \[basic.lookup.elab\]](basic.lookup.elab.md)
- [6.5.5 Class member access \[basic.lookup.classref\]](basic.lookup.classref.md)
- [6.5.6 Using-directives and namespace aliases \[basic.lookup.udir\]](basic.lookup.udir.md)
