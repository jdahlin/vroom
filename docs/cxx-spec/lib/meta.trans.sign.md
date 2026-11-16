# 20.15.7.3 Sign modifications [meta.trans.sign]

[Contents](../README.md) · up: [20.15.7 Transformations between types \[meta.trans\]](meta.trans.md) · prev: [20.15.7.2 Reference modifications \[meta.trans.ref\]](meta.trans.ref.md) · next: [20.15.7.4 Array modifications \[meta.trans.arr\]](meta.trans.arr.md)

<a id="tab:meta.trans.sign"></a>
**Table 54: Sign modifications** [tab:meta.trans.sign]

| Template | Comments |
| --- | --- |
| `template<class T>`<br>`struct make_signed;` | If `T` names a (possibly cv-qualified) signed integer type ([[basic.fundamental]](../basic.fundamental.md)) then the member typedef `type` names the type `T`; otherwise, if `T` names a (possibly cv-qualified) unsigned integer type then `type` names the corresponding signed integer type, with the same cv-qualifiers as `T`; otherwise, `type` names the signed integer type with smallest rank ([[conv.rank]](../conv.rank.md)) for which `sizeof(T) == sizeof(type)`, with the same cv-qualifiers as `T`.<br>*Mandates:* `T` is an integral or enumeration type other than *cv* `bool`. |
| `template<class T>`<br>`struct make_unsigned;` | If `T` names a (possibly cv-qualified) unsigned integer type ([[basic.fundamental]](../basic.fundamental.md)) then the member typedef `type` names the type `T`; otherwise, if `T` names a (possibly cv-qualified) signed integer type then `type` names the corresponding unsigned integer type, with the same cv-qualifiers as `T`; otherwise, `type` names the unsigned integer type with smallest rank ([[conv.rank]](../conv.rank.md)) for which `sizeof(T) == sizeof(type)`, with the same cv-qualifiers as `T`.<br>*Mandates:* `T` is an integral or enumeration type other than *cv* `bool`. |
