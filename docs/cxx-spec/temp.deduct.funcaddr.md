# 13.10.2.2 Deducing template arguments taking the address of a function template [temp.deduct.funcaddr]

[Contents](README.md) · up: [13.10.2 Template argument deduction \[temp.deduct\]](temp.deduct.md) · prev: [13.10.2.1 Deducing template arguments from a function call \[temp.deduct.call\]](temp.deduct.call.md) · next: [13.10.2.3 Deducing conversion function template arguments \[temp.deduct.conv\]](temp.deduct.conv.md)

**1** Template arguments can be deduced from the type specified when taking the address of an overloaded function ([[over.over]](over.over.md)). If there is a target, the function template’s function type and the target type are used as the types of `P` and `A`, and the deduction is done as described in [[temp.deduct.type]](temp.deduct.type.md). Otherwise, deduction is performed with empty sets of types P and A.

**2** A placeholder type ([[dcl.spec.auto]](dcl.spec.auto.md)) in the return type of a function template is a non-deduced context. If template argument deduction succeeds for such a function, the return type is determined from instantiation of the function body.
