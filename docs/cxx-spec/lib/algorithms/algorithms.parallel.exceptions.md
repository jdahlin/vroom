# 25.3.4 Parallel algorithm exceptions [algorithms.parallel.exceptions]

[Contents](../../README.md) · up: [25.3 Parallel algorithms \[algorithms.parallel\]](algorithms.parallel.md) · prev: [25.3.3 Effect of execution policies on algorithm execution \[algorithms.parallel.exec\]](algorithms.parallel.exec.md) · next: [25.3.5 `ExecutionPolicy` algorithm overloads \[algorithms.parallel.overloads\]](algorithms.parallel.overloads.md)

**1** During the execution of a parallel algorithm, if temporary memory resources are required for parallelization and none are available, the algorithm throws a `bad_alloc` exception.

**2** During the execution of a parallel algorithm, if the invocation of an element access function exits via an uncaught exception, the behavior is determined by the `ExecutionPolicy`.
