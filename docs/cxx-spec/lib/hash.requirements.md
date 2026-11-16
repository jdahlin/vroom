# 16.5.3.4 *Cpp17Hash* requirements [hash.requirements]

[Contents](../README.md) · up: [16.5.3 Requirements on types and expressions \[utility.requirements\]](utility.requirements.md) · prev: [16.5.3.3 *Cpp17NullablePointer* requirements \[nullablepointer.requirements\]](nullablepointer.requirements.md) · next: [16.5.3.5 *Cpp17Allocator* requirements \[allocator.requirements\]](allocator.requirements.md)

**1** A type `H` meets the *Cpp17Hash* requirements if:

- it is a function object type ([[function.objects]](function.objects.md)),
- it meets the *Cpp17CopyConstructible* ([Table 29](utility.arg.requirements.md#tab:cpp17.copyconstructible)) and *Cpp17Destructible* ([Table 32](utility.arg.requirements.md#tab:cpp17.destructible)) requirements, and
- the expressions shown in [Table 34](hash.requirements.md#tab:cpp17.hash) are valid and have the indicated semantics.

**2** Given `Key` is an argument type for function objects of type `H`, in [Table 34](hash.requirements.md#tab:cpp17.hash) `h` is a value of type (possibly `const`) `H`, `u` is an lvalue of type `Key`, and `k` is a value of a type convertible to (possibly `const`) `Key`.

<a id="tab:cpp17.hash"></a>
**Table 34: *Cpp17Hash* requirements** [tab:cpp17.hash]

| Expression | Return type | Requirement |
| --- | --- | --- |
| `h(k)` | `size_t` | The value returned shall depend only on the argument `k` for the duration of the program. [*Note 1*: Thus all evaluations of the expression `h(k)` with the same value for `k` yield the same result for a given execution of the program. — *end note*] [*Note 2*: For two different values `t1` and `t2`, the probability that `h(t1)` and `h(t2)` compare equal should be very small, approaching `1.0 / numeric_limits<size_t>::max()`. — *end note*] |
| `h(u)` | `size_t` | Shall not modify `u`. |
