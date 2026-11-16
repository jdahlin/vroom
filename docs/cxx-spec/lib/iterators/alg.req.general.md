# 23.3.7.1 General [alg.req.general]

[Contents](../../README.md) · up: [23.3.7 Common algorithm requirements \[alg.req\]](alg.req.md) · prev: [23.3.7 Common algorithm requirements \[alg.req\]](alg.req.md) · next: [23.3.7.2 Concept `indirectly_movable` \[alg.req.ind.move\]](alg.req.ind.move.md)

**1** There are several additional iterator concepts that are commonly applied to families of algorithms. These group together iterator requirements of algorithm families. There are three relational concepts that specify how element values are transferred between `indirectly_readable` and `indirectly_writable` types: `indirectly_movable`, `indirectly_copyable`, and `indirectly_swappable`. There are three relational concepts for rearrangements: `permutable`, `mergeable`, and `sortable`. There is one relational concept for comparing values from different sequences: `indirectly_comparable`.

**2** [*Note 1*: The `ranges::less` function object type used in the concepts below imposes constraints on the concepts’ arguments in addition to those that appear in the concepts’ bodies ([[range.cmp]](../utilities/range.cmp.md)). — *end note*]
