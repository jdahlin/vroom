# 20.15.7.5 Pointer modifications [meta.trans.ptr]

[Contents](../README.md) · up: [20.15.7 Transformations between types \[meta.trans\]](meta.trans.md) · prev: [20.15.7.4 Array modifications \[meta.trans.arr\]](meta.trans.arr.md) · next: [20.15.7.6 Other transformations \[meta.trans.other\]](meta.trans.other.md)

<a id="tab:meta.trans.ptr"></a>
**Table 56: Pointer modifications** [tab:meta.trans.ptr]

| Template | Comments |
| --- | --- |
| `template<class T> struct remove_pointer;` | If `T` has type “(possibly cv-qualified) pointer to `T1`” then the member typedef `type` names `T1`; otherwise, it names `T`. |
| `template<class T> struct add_pointer;` | If `T` names a referenceable type ([[defns.referenceable]](definitions.md#defns.referenceable)) or a *cv* `void` type then the member typedef `type` names the same type as `remove_reference_t<T>*`; otherwise, `type` names `T`. |
