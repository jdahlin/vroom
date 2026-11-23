# 16.5.4.8 Other functions [res.on.functions]

[Contents](../../README.md) · up: [16.5.4 Constraints on programs \[constraints\]](constraints.md) · prev: [16.5.4.7 Handler functions \[handler.functions\]](handler.functions.md) · next: [16.5.4.9 Function arguments \[res.on.arguments\]](res.on.arguments.md)

**1** In certain cases (replacement functions, handler functions, operations on types used to instantiate standard library template components), the C++ standard library depends on components supplied by a C++ program. If these components do not meet their requirements, this document places no requirements on the implementation.

**2** In particular, the effects are undefined in the following cases:

- For replacement functions ([[new.delete]](../support/new.delete.md)), if the installed replacement function does not implement the semantics of the applicable *Required behavior:* paragraph.
- For handler functions ([[new.handler]](../support/new.handler.md), [[terminate.handler]](../support/terminate.handler.md)), if the installed handler function does not implement the semantics of the applicable *Required behavior:* paragraph.
- For types used as template arguments when instantiating a template component, if the operations on the type do not implement the semantics of the applicable *Requirements* subclause ([[allocator.requirements]](allocator.requirements.md), [[container.requirements]](../containers/container.requirements.md), [[iterator.requirements]](../iterators/iterator.requirements.md), [[algorithms.requirements]](../algorithms/algorithms.requirements.md), [[numeric.requirements]](../numerics/numeric.requirements.md)). Operations on such types can report a failure by throwing an exception unless otherwise specified.
- If any replacement function or handler function or destructor operation exits via an exception, unless specifically allowed in the applicable *Required behavior:* paragraph.
- If an incomplete type ([[basic.types]](../../basic/basic.types.md)) is used as a template argument when instantiating a template component or evaluating a concept, unless specifically allowed for that component.
