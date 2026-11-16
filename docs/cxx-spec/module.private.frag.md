# 10.5 Private module fragment [module.private.frag]

[Contents](README.md) · up: [10 Modules \[module\]](module.md) · prev: [10.4 Global module fragment \[module.global.frag\]](module.global.frag.md) · next: [10.6 Instantiation context \[module.context\]](module.context.md)

```bnf
private-module-fragment:
    module-keyword ':' 'private' ';' declaration-seqₒₚₜ
```

**1** A *private-module-fragment* shall appear only in a primary module interface unit ([[module.unit]](module.unit.md)). A module unit with a *private-module-fragment* shall be the only module unit of its module; no diagnostic is required.

**2** [*Note 1*: A *private-module-fragment* ends the portion of the module interface unit that can affect the behavior of other translation units. A *private-module-fragment* allows a module to be represented as a single translation unit without making all of the contents of the module reachable to importers. The presence of a *private-module-fragment* affects:

- the point by which the definition of an exported inline function is required ([[dcl.inline]](dcl.inline.md)),
- the point by which the definition of an exported function with a placeholder return type is required ([[dcl.spec.auto]](dcl.spec.auto.md)),
- whether a declaration is required not to be an exposure ([[basic.link]](basic.link.md)),
- where definitions for inline functions and templates must appear ([[basic.def.odr]](basic.def.odr.md), [[dcl.inline]](dcl.inline.md), [[temp.pre]](temp.pre.md)),
- the instantiation contexts of templates instantiated before it ([[module.context]](module.context.md)), and
- the reachability of declarations within it ([[module.reach]](module.reach.md)).

— *end note*]

**3** [*Example 1*:

```cpp
export module A;
export inline void fn_e();      // error: exported inline function fn_e not defined
                                // before private module fragment
inline void fn_m();             // OK, module-linkage inline function
static void fn_s();
export struct X;
export void g(X *x) {
  fn_s();                       // OK, call to static function in same translation unit
  fn_m();                       // OK, call to module-linkage inline function
}
export X *factory();            // OK

module :private;
struct X {};                    // definition not reachable from importers of A
X *factory() {
  return new X ();
}
void fn_e() {}
void fn_m() {}
void fn_s() {}
```

— *end example*]
