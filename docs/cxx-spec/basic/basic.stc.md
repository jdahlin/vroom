# 6.7.5 Storage duration [basic.stc]

[Contents](../README.md) · up: [6.7 Memory and objects \[basic.memobj\]](basic.memobj.md) · prev: [6.7.4 Indeterminate values \[basic.indet\]](basic.indet.md) · next: [6.7.5.1 Static storage duration \[basic.stc.static\]](basic.stc.static.md)

**1** The *storage duration* is the property of an object that defines the minimum potential lifetime of the storage containing the object. The storage duration is determined by the construct used to create the object and is one of the following:

- static storage duration
- thread storage duration
- automatic storage duration
- dynamic storage duration

**2** Static, thread, and automatic storage durations are associated with objects introduced by declarations ([[basic.def]](basic.def.md)) and implicitly created by the implementation ([[class.temporary]](class.temporary.md)). The dynamic storage duration is associated with objects created by a *new-expression* ([[expr.new]](../expr/expr.new.md)).

**3** The storage duration categories apply to references as well.

**4** When the end of the duration of a region of storage is reached, the values of all pointers representing the address of any part of that region of storage become invalid pointer values ([[basic.compound]](basic.compound.md)). Indirection through an invalid pointer value and passing an invalid pointer value to a deallocation function have undefined behavior. Any other use of an invalid pointer value has implementation-defined behavior.[^1]

## Subclauses

- [6.7.5.1 Static storage duration \[basic.stc.static\]](basic.stc.static.md)
- [6.7.5.2 Thread storage duration \[basic.stc.thread\]](basic.stc.thread.md)
- [6.7.5.3 Automatic storage duration \[basic.stc.auto\]](basic.stc.auto.md)
- [6.7.5.4 Dynamic storage duration \[basic.stc.dynamic\]](basic.stc.dynamic.md)
  - [6.7.5.4.1 Allocation functions \[basic.stc.dynamic.allocation\]](basic.stc.dynamic.allocation.md)
  - [6.7.5.4.2 Deallocation functions \[basic.stc.dynamic.deallocation\]](basic.stc.dynamic.deallocation.md)
  - [6.7.5.4.3 Safely-derived pointers \[basic.stc.dynamic.safety\]](basic.stc.dynamic.safety.md)
- [6.7.5.5 Duration of subobjects \[basic.stc.inherit\]](basic.stc.inherit.md)

[^1]: Some implementations might define that copying an invalid pointer value causes a system-generated runtime fault.
