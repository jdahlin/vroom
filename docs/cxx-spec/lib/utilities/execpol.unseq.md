# 20.18.7 Unsequenced execution policy [execpol.unseq]

[Contents](../../README.md) · up: [20.18 Execution policies \[execpol\]](execpol.md) · prev: [20.18.6 Parallel and unsequenced execution policy \[execpol.parunseq\]](execpol.parunseq.md) · next: [20.18.8 Execution policy objects \[execpol.objects\]](execpol.objects.md)

```cpp
class execution::unsequenced_policy { unspecified };
```

**1** The class `unsequenced_policy` is an execution policy type used as a unique type to disambiguate parallel algorithm overloading and indicate that a parallel algorithm’s execution may be vectorized, e.g., executed on a single thread using instructions that operate on multiple data items.

**2** During the execution of a parallel algorithm with the `execution::unsequenced_policy` policy, if the invocation of an element access function exits via an uncaught exception, `terminate()` is called.
