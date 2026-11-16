# 13.10.2.3 Deducing conversion function template arguments [temp.deduct.conv]

[Contents](README.md) · up: [13.10.2 Template argument deduction \[temp.deduct\]](temp.deduct.md) · prev: [13.10.2.2 Deducing template arguments taking the address of a function template \[temp.deduct.funcaddr\]](temp.deduct.funcaddr.md) · next: [13.10.2.4 Deducing template arguments during partial ordering \[temp.deduct.partial\]](temp.deduct.partial.md)

**1** Template argument deduction is done by comparing the return type of the conversion function template (call it `P`) with the type that is required as the result of the conversion (call it `A`; see [[dcl.init]](dcl.init.md), [[over.match.conv]](over.match.conv.md), and [[over.match.ref]](over.match.ref.md) for the determination of that type) as described in [[temp.deduct.type]](temp.deduct.type.md).

**2** If `P` is a reference type, the type referred to by `P` is used in place of `P` for type deduction and for any further references to or transformations of `P` in the remainder of this subclause.

**3** If `A` is not a reference type:

- If `P` is an array type, the pointer type produced by the array-to-pointer standard conversion ([[conv.array]](conv.array.md)) is used in place of `P` for type deduction; otherwise,
- If `P` is a function type, the pointer type produced by the function-to-pointer standard conversion ([[conv.func]](conv.func.md)) is used in place of `P` for type deduction; otherwise,
- If `P` is a cv-qualified type, the top-level cv-qualifiers of `P`’s type are ignored for type deduction.

**4** If `A` is a cv-qualified type, the top-level cv-qualifiers of `A`’s type are ignored for type deduction. If `A` is a reference type, the type referred to by `A` is used for type deduction.

**5** In general, the deduction process attempts to find template argument values that will make the deduced `A` identical to `A`. However, there are four cases that allow a difference:

- If the original `A` is a reference type, `A` can be more cv-qualified than the deduced `A` (i.e., the type referred to by the reference).
- If the original `A` is a function pointer type, `A` can be “pointer to function” even if the deduced `A` is “pointer to `noexcept` function”.
- If the original `A` is a pointer-to-member-function type, `A` can be “pointer to member of type function” even if the deduced `A` is “pointer to member of type `noexcept` function”.
- The deduced `A` can be another pointer or pointer-to-member type that can be converted to `A` via a qualification conversion.

**6** These alternatives are considered only if type deduction would otherwise fail. If they yield more than one possible deduced `A`, the type deduction fails.
