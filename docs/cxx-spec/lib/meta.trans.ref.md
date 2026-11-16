# 20.15.7.2 Reference modifications [meta.trans.ref]

[Contents](../README.md) · up: [20.15.7 Transformations between types \[meta.trans\]](meta.trans.md) · prev: [20.15.7.1 Const-volatile modifications \[meta.trans.cv\]](meta.trans.cv.md) · next: [20.15.7.3 Sign modifications \[meta.trans.sign\]](meta.trans.sign.md)

<a id="tab:meta.trans.ref"></a>
**Table 53: Reference modifications** [tab:meta.trans.ref]

| Template | Comments |
| --- | --- |
| `template<class T> struct remove_reference;` | If `T` has type “reference to `T1`” then the member typedef `type` names `T1`; otherwise, `type` names `T`. |
| `template<class T> struct add_lvalue_reference;` | If `T` names a referenceable type ([[defns.referenceable]](definitions.md#defns.referenceable)) then the member typedef `type` names `T&`; otherwise, `type` names `T`. [*Note 1*: This rule reflects the semantics of reference collapsing ([[dcl.ref]](../dcl.ref.md)). — *end note*] |
| `template<class T>`<br>`struct add_rvalue_reference;` | If `T` names a referenceable type then the member typedef `type` names `T&&`; otherwise, `type` names `T`. [*Note 2*: This rule reflects the semantics of reference collapsing ([[dcl.ref]](../dcl.ref.md)). For example, when a type `T` names a type `T1&`, the type `add_rvalue_reference_t<T>` is not an rvalue reference. — *end note*] |
