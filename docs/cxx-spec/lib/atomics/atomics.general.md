# 31.1 General [atomics.general]

[Contents](../../README.md) · up: [31 Atomic operations library \[atomics\]](atomics.md) · prev: [31 Atomic operations library \[atomics\]](atomics.md) · next: [31.2 Header `<atomic>` synopsis \[atomics.syn\]](atomics.syn.md)

**1** This Clause describes components for fine-grained atomic access. This access is provided via operations on atomic objects.

**2** The following subclauses describe atomics requirements and components for types and operations, as summarized in [Table 143](atomics.general.md#tab:atomics.summary).

<a id="tab:atomics.summary"></a>
**Table 143: Atomics library summary** [tab:atomics.summary]

|  |  |  |
| --- | --- | --- |
| [[atomics.alias]](atomics.alias.md) | Type aliases | `<atomic>` |
| [[atomics.order]](atomics.order.md) | Order and consistency |  |
| [[atomics.lockfree]](atomics.lockfree.md) | Lock-free property |  |
| [[atomics.wait]](atomics.wait.md) | Waiting and notifying |  |
| [[atomics.ref.generic]](atomics.ref.generic.md) | Class template `atomic_ref` |  |
| [[atomics.types.generic]](atomics.types.generic.md) | Class template `atomic` |  |
| [[atomics.nonmembers]](atomics.nonmembers.md) | Non-member functions |  |
| [[atomics.flag]](atomics.flag.md) | Flag type and operations |  |
| [[atomics.fences]](atomics.fences.md) | Fences |  |
