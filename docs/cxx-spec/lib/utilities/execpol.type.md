# 20.18.3 Execution policy type trait [execpol.type]

[Contents](../../README.md) · up: [20.18 Execution policies \[execpol\]](execpol.md) · prev: [20.18.2 Header `<execution>` synopsis \[execution.syn\]](execution.syn.md) · next: [20.18.4 Sequenced execution policy \[execpol.seq\]](execpol.seq.md)

```cpp
template<class T> struct is_execution_policy { see below };
```

**1** `is_execution_policy` can be used to detect execution policies for the purpose of excluding function signatures from otherwise ambiguous overload resolution participation.

**2** `is_execution_policy<T>` is a *Cpp17UnaryTypeTrait* with a base characteristic of `true_type` if `T` is the type of a standard or implementation-defined execution policy, otherwise `false_type`.

[*Note 1*: This provision reserves the privilege of creating non-standard execution policies to the library implementation. — *end note*]

**3** The behavior of a program that adds specializations for `is_execution_policy` is undefined.
