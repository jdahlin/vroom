# 20.18.6 Parallel and unsequenced execution policy [execpol.parunseq]

[Contents](../README.md) · up: [20.18 Execution policies \[execpol\]](execpol.md) · prev: [20.18.5 Parallel execution policy \[execpol.par\]](execpol.par.md) · next: [20.18.7 Unsequenced execution policy \[execpol.unseq\]](execpol.unseq.md)

```cpp
class execution::parallel_unsequenced_policy { unspecified };
```

**1** The class `execution::parallel_unsequenced_policy` is an execution policy type used as a unique type to disambiguate parallel algorithm overloading and indicate that a parallel algorithm’s execution may be parallelized and vectorized.

**2** During the execution of a parallel algorithm with the `execution::parallel_unsequenced_policy` policy, if the invocation of an element access function exits via an uncaught exception, `terminate()` is called.
