# 25.3.2 Requirements on user-provided function objects [algorithms.parallel.user]

[Contents](../../README.md) · up: [25.3 Parallel algorithms \[algorithms.parallel\]](algorithms.parallel.md) · prev: [25.3.1 Preamble \[algorithms.parallel.defns\]](algorithms.parallel.defns.md) · next: [25.3.3 Effect of execution policies on algorithm execution \[algorithms.parallel.exec\]](algorithms.parallel.exec.md)

**1** Unless otherwise specified, function objects passed into parallel algorithms as objects of type `Predicate`, `BinaryPredicate`, `Compare`, `UnaryOperation`, `BinaryOperation`, `BinaryOperation1`, `BinaryOperation2`, and the operators used by the analogous overloads to these parallel algorithms that could be formed by the invocation with the specified default predicate or operation (where applicable) shall not directly or indirectly modify objects via their arguments, nor shall they rely on the identity of the provided objects.
