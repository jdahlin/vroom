# 16.5.4.12 Semantic requirements [res.on.requirements]

[Contents](../../README.md) · up: [16.5.4 Constraints on programs \[constraints\]](constraints.md) · prev: [16.5.4.11 Expects paragraph \[res.on.expects\]](res.on.expects.md) · next: [16.5.5 Conforming implementations \[conforming\]](conforming.md)

**1** A sequence `Args` of template arguments is said to *model* a concept `C` if `Args` satisfies `C` ([[temp.constr.decl]](../../temp/temp.constr.decl.md)) and meets all semantic requirements (if any) given in the specification of `C`.

**2** If the validity or meaning of a program depends on whether a sequence of template arguments models a concept, and the concept is satisfied but not modeled, the program is ill-formed, no diagnostic required.

**3** If the semantic requirements of a declaration’s constraints ([[structure.requirements]](structure.requirements.md)) are not modeled at the point of use, the program is ill-formed, no diagnostic required.
