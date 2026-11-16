# 25.3.5 `ExecutionPolicy` algorithm overloads [algorithms.parallel.overloads]

[Contents](../README.md) · up: [25.3 Parallel algorithms \[algorithms.parallel\]](algorithms.parallel.md) · prev: [25.3.4 Parallel algorithm exceptions \[algorithms.parallel.exceptions\]](algorithms.parallel.exceptions.md) · next: [25.4 Header `<algorithm>` synopsis \[algorithm.syn\]](algorithm.syn.md)

**1** Parallel algorithms are algorithm overloads. Each parallel algorithm overload has an additional template type parameter named `ExecutionPolicy`, which is the first template parameter. Additionally, each parallel algorithm overload has an additional function parameter of type `ExecutionPolicy&&`, which is the first function parameter. [*Note 1*: Not all algorithms have parallel algorithm overloads. — *end note*]

**2** Unless otherwise specified, the semantics of `ExecutionPolicy` algorithm overloads are identical to their overloads without.

**3** Unless otherwise specified, the complexity requirements of `ExecutionPolicy` algorithm overloads are relaxed from the complexity requirements of the overloads without as follows: when the guarantee says “at most *expr*” or “exactly *expr*” and does not specify the number of assignments or swaps, and *expr* is not already expressed with 𝑂() notation, the complexity of the algorithm shall be 𝑂(*expr*).

**4** Parallel algorithms shall not participate in overload resolution unless `is_execution_policy_v<remove_cvref_t<ExecutionPolicy>>` is `true`.
