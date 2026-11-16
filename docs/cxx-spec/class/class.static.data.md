# 11.4.8.2 Static data members [class.static.data]

[Contents](../README.md) · up: [11.4.8 Static members \[class.static\]](class.static.md) · prev: [11.4.8.1 Static member functions \[class.static.mfct\]](class.static.mfct.md) · next: [11.4.9 Bit-fields \[class.bit\]](class.bit.md)

**1** A static data member is not part of the subobjects of a class. If a static data member is declared `thread_local` there is one copy of the member per thread. If a static data member is not declared `thread_local` there is one copy of the data member that is shared by all the objects of the class.

**2** A static data member shall not be `mutable` ([[dcl.stc]](../dcl/dcl.stc.md)). A static data member shall not be a direct member ([[class.mem]](class.mem.md)) of an unnamed ([[class.pre]](class.pre.md)) or local ([[class.local]](class.local.md)) class or of a (possibly indirectly) nested class ([[class.nest]](class.nest.md)) thereof.

**3** The declaration of a non-inline static data member in its class definition is not a definition and may be of an incomplete type other than *cv* `void`. The definition for a static data member that is not defined inline in the class definition shall appear in a namespace scope enclosing the member’s class definition. In the definition at namespace scope, the name of the static data member shall be qualified by its class name using the `::` operator. The *initializer* expression in the definition of a static data member is in the scope of its class ([[basic.scope.class]](../basic/basic.scope.class.md)). [*Example 1*:

```cpp
class process {
  static process* run_chain;
  static process* running;
};

process* process::running = get_main();
process* process::run_chain = running;
```

The static data member `run_chain` of class `process` is defined in global scope; the notation `process::run_chain` specifies that the member `run_chain` is a member of class `process` and in the scope of class `process`. In the static data member definition, the *initializer* expression refers to the static data member `running` of class `process`. — *end example*]

[*Note 1*: Once the static data member has been defined, it exists even if no objects of its class have been created. [*Example 2*: In the example above, `run_chain` and `running` exist even if no objects of class `process` are created by the program. — *end example*] — *end note*]

**4** If a non-volatile non-inline `const` static data member is of integral or enumeration type, its declaration in the class definition can specify a *brace-or-equal-initializer* in which every *initializer-clause* that is an *assignment-expression* is a constant expression ([[expr.const]](../expr/expr.const.md)). The member shall still be defined in a namespace scope if it is odr-used ([[basic.def.odr]](../basic/basic.def.odr.md)) in the program and the namespace scope definition shall not contain an *initializer*. An inline static data member may be defined in the class definition and may specify a *brace-or-equal-initializer*. If the member is declared with the `constexpr` specifier, it may be redeclared in namespace scope with no initializer (this usage is deprecated; see [[depr.static.constexpr]](../depr/depr.static.constexpr.md)). Declarations of other static data members shall not specify a *brace-or-equal-initializer*.

**5** [*Note 2*: There is exactly one definition of a static data member that is odr-used ([[basic.def.odr]](../basic/basic.def.odr.md)) in a valid program. — *end note*]

**6** [*Note 3*: Static data members of a class in namespace scope have the linkage of the name of the class ([[basic.link]](../basic/basic.link.md)). — *end note*]

**7** Static data members are initialized and destroyed exactly like non-local variables ([[basic.start.static]](../basic/basic.start.static.md), [[basic.start.dynamic]](../basic/basic.start.dynamic.md), [[basic.start.term]](../basic/basic.start.term.md)).
