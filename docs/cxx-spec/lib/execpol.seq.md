# 20.18.4 Sequenced execution policy [execpol.seq]

[Contents](../README.md) · up: [20.18 Execution policies \[execpol\]](execpol.md) · prev: [20.18.3 Execution policy type trait \[execpol.type\]](execpol.type.md) · next: [20.18.5 Parallel execution policy \[execpol.par\]](execpol.par.md)

```cpp
class execution::sequenced_policy { unspecified };
```

**1** The class `execution::sequenced_policy` is an execution policy type used as a unique type to disambiguate parallel algorithm overloading and require that a parallel algorithm’s execution may not be parallelized.

**2** During the execution of a parallel algorithm with the `execution::sequenced_policy` policy, if the invocation of an element access function exits via an uncaught exception, `terminate()` is called.
