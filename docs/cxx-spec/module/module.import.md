# 10.3 Import declaration [module.import]

[Contents](../README.md) · up: [10 Modules \[module\]](module.md) · prev: [10.2 Export declaration \[module.interface\]](module.interface.md) · next: [10.4 Global module fragment \[module.global.frag\]](module.global.frag.md)

```bnf
module-import-declaration:
    import-keyword module-name attribute-specifier-seqₒₚₜ ';'
    import-keyword module-partition attribute-specifier-seqₒₚₜ ';'
    import-keyword header-name attribute-specifier-seqₒₚₜ ';'
```

**1** A *module-import-declaration* shall only appear at global namespace scope. In a module unit, all *module-import-declaration*s and *export-declaration*s exporting *module-import-declaration*s shall precede all other *declaration*s in the *declaration-seq* of the *translation-unit* and of the *private-module-fragment* (if any). The optional *attribute-specifier-seq* appertains to the *module-import-declaration*.

**2** A *module-import-declaration* *imports* a set of translation units determined as described below. [*Note 1*: Namespace-scope names exported by the imported translation units become visible ([[basic.scope.namespace]](../basic/basic.scope.namespace.md)) in the importing translation unit and declarations within the imported translation units become reachable ([[module.reach]](module.reach.md)) in the importing translation unit after the import declaration. — *end note*]

**3** A *module-import-declaration* that specifies a *module-name* `M` imports all module interface units of `M`.

**4** A *module-import-declaration* that specifies a *module-partition* shall only appear after the *module-declaration* in a module unit of some module `M`. Such a declaration imports the so-named module partition of `M`.

**5** A *module-import-declaration* that specifies a *header-name* `H` imports a synthesized *header unit*, which is a translation unit formed by applying phases 1 to 7 of translation ([[lex.phases]](../lex/lex.phases.md)) to the source file or header nominated by `H`, which shall not contain a *module-declaration*. [*Note 2*: All declarations within a header unit are implicitly exported ([[module.interface]](module.interface.md)), and are attached to the global module ([[module.unit]](module.unit.md)). — *end note*] An *importable header* is a member of an implementation-defined set of headers that includes all importable C++ library headers ([[headers]](../lib/library/headers.md)). `H` shall identify an importable header. Given two such *module-import-declaration*s:

- if their *header-name*s identify different headers or source files ([[cpp.include]](../cpp/cpp.include.md)), they import distinct header units;
- otherwise, if they appear in the same translation unit, they import the same header unit;
- otherwise, it is unspecified whether they import the same header unit. [*Note 3*: It is therefore possible that multiple copies exist of entities declared with internal linkage in an importable header. — *end note*]

[*Note 4*: A *module-import-declaration* nominating a *header-name* is also recognized by the preprocessor, and results in macros defined at the end of phase 4 of translation of the header unit being made visible as described in [[cpp.import]](../cpp/cpp.import.md). — *end note*]

**6** A declaration of a name with internal linkage is permitted within a header unit despite all declarations being implicitly exported ([[module.interface]](module.interface.md)). [*Note 5*: A definition that appears in multiple translation units cannot in general refer to such names ([[basic.def.odr]](../basic/basic.def.odr.md)). — *end note*] A header unit shall not contain a definition of a non-inline function or variable whose name has external linkage.

**7** When a *module-import-declaration* imports a translation unit *T*, it also imports all translation units imported by exported *module-import-declaration*s in *T*; such translation units are said to be *exported* by *T*. Additionally, when a *module-import-declaration* in a module unit of some module *M* imports another module unit *U* of *M*, it also imports all translation units imported by non-exported *module-import-declaration*s in the module unit purview of *U*.[^1] These rules may in turn lead to the importation of yet more translation units.

**8** A module implementation unit shall not be exported. [*Example 1*:

*Translation unit #1:*

```cpp
module M:Part;
```

*Translation unit #2:*

```cpp
export module M;
export import :Part;    // error: exported partition :Part is an implementation unit
```

— *end example*]

**9** A module implementation unit of a module `M` that is not a module partition shall not contain a *module-import-declaration* nominating `M`. [*Example 2*:

```cpp
module M;
import M;               // error: cannot import M in its own unit
```

— *end example*]

**10** A translation unit has an *interface dependency* on a translation unit `U` if it contains a declaration (possibly a *module-declaration*) that imports `U` or if it has an interface dependency on a translation unit that has an interface dependency on `U`. A translation unit shall not have an interface dependency on itself. [*Example 3*:

*Interface unit of `M1`:*

```cpp
export module M1;
import M2;
```

*Interface unit of `M2`:*

```cpp
export module M2;
import M3;
```

*Interface unit of `M3`:*

```cpp
export module M3;
import M1;              // error: cyclic interface dependency M3 → M1 → M2 → M3
```

— *end example*]

[^1]: This is consistent with the rules for visibility of imported names ([[basic.scope.namespace]](../basic/basic.scope.namespace.md)).
