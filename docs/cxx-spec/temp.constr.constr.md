# 13.5.1 Constraints [temp.constr.constr]

[Contents](README.md) · up: [13.5 Template constraints \[temp.constr\]](temp.constr.md) · prev: [13.5 Template constraints \[temp.constr\]](temp.constr.md) · next: [13.5.1.1 Logical operations \[temp.constr.op\]](temp.constr.op.md)

**1** A *constraint* is a sequence of logical operations and operands that specifies requirements on template arguments. The operands of a logical operation are constraints. There are three different kinds of constraints:

- conjunctions ([[temp.constr.op]](temp.constr.op.md)),
- disjunctions ([[temp.constr.op]](temp.constr.op.md)), and
- atomic constraints ([[temp.constr.atomic]](temp.constr.atomic.md)).

**2** In order for a constrained template to be instantiated ([[temp.spec]](temp.spec.md)), its associated constraints ([[temp.constr.decl]](temp.constr.decl.md)) shall be satisfied as described in the following subclauses. [*Note 1*: Forming the name of a specialization of a class template, a variable template, or an alias template ([[temp.names]](temp.names.md)) requires the satisfaction of its constraints. Overload resolution ([[over.match.viable]](over.match.viable.md)) requires the satisfaction of constraints on functions and function templates. — *end note*]

## Subclauses

- [13.5.1.1 Logical operations \[temp.constr.op\]](temp.constr.op.md)
- [13.5.1.2 Atomic constraints \[temp.constr.atomic\]](temp.constr.atomic.md)
