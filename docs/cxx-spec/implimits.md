# Annex B (informative) Implementation quantities [implimits]

[Contents](README.md) · prev: [A.12 Preprocessing directives \[gram.cpp\]](gram.cpp.md) · next: [Annex C Compatibility \[diff\]](diff.md)

**1** Because computers are finite, C++ implementations are inevitably limited in the size of the programs they can successfully process. Every implementation shall document those limitations where known. This documentation may cite fixed limits where they exist, say how to compute variable limits as a function of available resources, or say that fixed limits do not exist or are unknown.

**2** The limits may constrain quantities that include those described below or others. The bracketed number following each quantity is recommended as the minimum for that quantity. However, these quantities are only guidelines and do not determine compliance.

- Nesting levels of compound statements ([[stmt.block]](stmt.block.md)), iteration control structures ([[stmt.iter]](stmt.iter.md)), and selection control structures ([[stmt.select]](stmt.select.md)) [256].
- Nesting levels of conditional inclusion ([[cpp.cond]](cpp.cond.md)) [256].
- Pointer ([[dcl.ptr]](dcl.ptr.md)), array ([[dcl.array]](dcl.array.md)), and function ([[dcl.fct]](dcl.fct.md)) declarators (in any combination) modifying a class, arithmetic, or incomplete type in a declaration [256].
- Nesting levels of parenthesized expressions ([[expr.prim.paren]](expr.prim.paren.md)) within a full-expression [256].
- Number of characters in an internal identifier ([[lex.name]](lex.name.md)) or macro name ([[cpp.replace]](cpp.replace.md)) [1024].
- Number of characters in an external identifier ([[lex.name]](lex.name.md), [[basic.link]](basic.link.md)) [1024].
- External identifiers ([[basic.link]](basic.link.md)) in one translation unit [65536].
- Identifiers with block scope declared in one block ([[basic.scope.block]](basic.scope.block.md)) [1024].
- Structured bindings ([[dcl.struct.bind]](dcl.struct.bind.md)) introduced in one declaration [256].
- Macro identifiers ([[cpp.replace]](cpp.replace.md)) simultaneously defined in one translation unit [65536].
- Parameters in one function definition ([[dcl.fct.def.general]](dcl.fct.def.general.md)) [256].
- Arguments in one function call ([[expr.call]](expr.call.md)) [256].
- Parameters in one macro definition ([[cpp.replace]](cpp.replace.md)) [256].
- Arguments in one macro invocation ([[cpp.replace]](cpp.replace.md)) [256].
- Characters in one logical source line ([[lex.phases]](lex.phases.md)) [65536].
- Characters in a *string-literal* ([[lex.string]](lex.string.md)) (after concatenation ([[lex.phases]](lex.phases.md))) [65536].
- Size of an object ([[intro.object]](intro.object.md)) [262144].
- Nesting levels for `#include` files ([[cpp.include]](cpp.include.md)) [256].
- Case labels for a `switch` statement ([[stmt.switch]](stmt.switch.md)) (excluding those for any nested `switch` statements) [16384].
- Non-static data members (including inherited ones) in a single class ([[class.mem]](class.mem.md)) [16384].
- Lambda-captures in one *lambda-expression* ([[expr.prim.lambda.capture]](expr.prim.lambda.capture.md)) [256].
- Enumeration constants in a single enumeration ([[dcl.enum]](dcl.enum.md)) [4096].
- Levels of nested class definitions ([[class.nest]](class.nest.md)) in a single *member-specification* [256].
- Functions registered by `atexit()` ([[support.start.term]](lib/support.start.term.md)) [32].
- Functions registered by `at_quick_exit()` ([[support.start.term]](lib/support.start.term.md)) [32].
- Direct and indirect base classes ([[class.derived]](class.derived.md)) [16384].
- Direct base classes for a single class ([[class.derived]](class.derived.md)) [1024].
- Class members declared in a single *member-specification* (including member functions) ([[class.mem]](class.mem.md)) [4096].
- Final overriding virtual functions in a class, accessible or not ([[class.virtual]](class.virtual.md)) [16384].
- Direct and indirect virtual bases of a class ([[class.mi]](class.mi.md)) [1024].
- Static data members of a class ([[class.static.data]](class.static.data.md)) [1024].
- Friend declarations in a class ([[class.friend]](class.friend.md)) [4096].
- Access control declarations in a class ([[class.access.spec]](class.access.spec.md)) [4096].
- Member initializers in a constructor definition ([[class.base.init]](class.base.init.md)) [6144].
- *initializer-clause*s in one *braced-init-list* ([[dcl.init]](dcl.init.md)) [16384].
- Scope qualifications of one identifier ([[expr.prim.id.qual]](expr.prim.id.qual.md)) [256].
- Nested *linkage-specification*s ([[dcl.link]](dcl.link.md)) [1024].
- Recursive constexpr function invocations ([[dcl.constexpr]](dcl.constexpr.md)) [512].
- Full-expressions evaluated within a core constant expression ([[expr.const]](expr.const.md)) [1048576].
- Template parameters in a template declaration ([[temp.param]](temp.param.md)) [1024].
- Recursively nested template instantiations ([[temp.inst]](temp.inst.md)), including substitution during template argument deduction ([[temp.deduct]](temp.deduct.md)) [1024].
- Handlers per try block ([[except.handle]](except.handle.md)) [256].
- Number of placeholders ([[func.bind.place]](lib/func.bind.place.md)) [10].
