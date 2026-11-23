# 10.1 Module units and purviews [module.unit]

[Contents](../README.md) · up: [10 Modules \[module\]](module.md) · prev: [10 Modules \[module\]](module.md) · next: [10.2 Export declaration \[module.interface\]](module.interface.md)

```bnf
module-declaration:
    export-keywordₒₚₜ module-keyword module-name module-partitionₒₚₜ attribute-specifier-seqₒₚₜ ';'
```

```bnf
module-name:
    module-name-qualifierₒₚₜ identifier
```

```bnf
module-partition:
    ':' module-name-qualifierₒₚₜ identifier
```

```bnf
module-name-qualifier:
    identifier '.'
    module-name-qualifier identifier '.'
```

**1** A *module unit* is a translation unit that contains a *module-declaration*. A *named module* is the collection of module units with the same *module-name*. The identifiers `module` and `import` shall not appear as *identifier*s in a *module-name* or *module-partition*. All *module-name*s either beginning with an *identifier* consisting of `std` followed by zero or more *digit*s or containing a reserved identifier ([[lex.name]](../lex/lex.name.md)) are reserved and shall not be specified in a *module-declaration*; no diagnostic is required. If any *identifier* in a reserved *module-name* is a reserved identifier, the module name is reserved for use by C++ implementations; otherwise it is reserved for future standardization. The optional *attribute-specifier-seq* appertains to the *module-declaration*.

**2** A *module interface unit* is a module unit whose *module-declaration* starts with *export-keyword*; any other module unit is a *module implementation unit*. A named module shall contain exactly one module interface unit with no *module-partition*, known as the *primary module interface unit* of the module; no diagnostic is required.

**3** A *module partition* is a module unit whose *module-declaration* contains a *module-partition*. A named module shall not contain multiple module partitions with the same *module-partition*. All module partitions of a module that are module interface units shall be directly or indirectly exported by the primary module interface unit ([[module.import]](module.import.md)). No diagnostic is required for a violation of these rules. [*Note 1*: Module partitions can be imported only by other module units in the same module. The division of a module into module units is not visible outside the module. — *end note*]

**4** [*Example 1*:

*Translation unit #1:*

```cpp
export module A;
export import :Foo;
export int baz();
```

*Translation unit #2:*

```cpp
export module A:Foo;
import :Internals;
export int foo() { return 2 * (bar() + 1); }
```

*Translation unit #3:*

```cpp
module A:Internals;
int bar();
```

*Translation unit #4:*

```cpp
module A;
import :Internals;
int bar() { return baz() - 10; }
int baz() { return 30; }
```

Module `A` contains four translation units:

- a primary module interface unit,
- a module partition `A:Foo`, which is a module interface unit forming part of the interface of module `A`,
- a module partition `A:Internals`, which does not contribute to the external interface of module `A`, and
- a module implementation unit providing a definition of `bar` and `baz`, which cannot be imported because it does not have a partition name.

— *end example*]

**5** A *module unit purview* is the sequence of *token*s starting at the *module-declaration* and extending to the end of the translation unit. The *purview* of a named module `M` is the set of module unit purviews of `M`’s module units.

**6** The *global module* is the collection of all *global-module-fragment*s and all translation units that are not module units. Declarations appearing in such a context are said to be in the *purview* of the global module. [*Note 2*: The global module has no name, no module interface unit, and is not introduced by any *module-declaration*. — *end note*]

**7** A *module* is either a named module or the global module. A declaration is *attached* to a module as follows:

- If the declaration

  - is a replaceable global allocation or deallocation function ([[new.delete.single]](../lib/support/new.delete.single.md), [[new.delete.array]](../lib/support/new.delete.array.md)), or
  - is a *namespace-definition* with external linkage, or
  - appears within a *linkage-specification*,

  it is attached to the global module.

- Otherwise, the declaration is attached to the module in whose purview it appears.

**8** A *module-declaration* that contains neither an *export-keyword* nor a *module-partition* implicitly imports the primary module interface unit of the module as if by a *module-import-declaration*. [*Example 2*:

*Translation unit #1:*

```cpp
module B:Y;                     // does not implicitly import B
int y();
```

*Translation unit #2:*

```cpp
export module B;
import :Y;                      // OK, does not create interface dependency cycle
int n = y();
```

*Translation unit #3:*

```cpp
module B:X1;                    // does not implicitly import B
int &a = n;                     // error: n not visible here
```

*Translation unit #4:*

```cpp
module B:X2;                    // does not implicitly import B
import B;
int &b = n;                     // OK
```

*Translation unit #5:*

```cpp
module B;                       // implicitly imports B
int &c = n;                     // OK
```

— *end example*]
