# 16.5.3.5.1 Allocator completeness requirements [allocator.requirements.completeness]

[Contents](../../README.md) · up: [16.5.3.5 *Cpp17Allocator* requirements \[allocator.requirements\]](allocator.requirements.md) · prev: [16.5.3.5 *Cpp17Allocator* requirements \[allocator.requirements\]](allocator.requirements.md) · next: [16.5.4 Constraints on programs \[constraints\]](constraints.md)

**1** If `X` is an allocator class for type `T`, `X` additionally meets the allocator completeness requirements if, whether or not `T` is a complete type:

- `X` is a complete type, and
- all the member types of `allocator_traits<X>` ([[allocator.traits]](../utilities/allocator.traits.md)) other than `value_type` are complete types.
