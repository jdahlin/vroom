# 20.18.5 Parallel execution policy [execpol.par]

[Contents](../README.md) · up: [20.18 Execution policies \[execpol\]](execpol.md) · prev: [20.18.4 Sequenced execution policy \[execpol.seq\]](execpol.seq.md) · next: [20.18.6 Parallel and unsequenced execution policy \[execpol.parunseq\]](execpol.parunseq.md)

```cpp
class execution::parallel_policy { unspecified };
```

**1** The class `execution::parallel_policy` is an execution policy type used as a unique type to disambiguate parallel algorithm overloading and indicate that a parallel algorithm’s execution may be parallelized.

**2** During the execution of a parallel algorithm with the `execution::parallel_policy` policy, if the invocation of an element access function exits via an uncaught exception, `terminate()` is called.
