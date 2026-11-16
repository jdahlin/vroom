# 20.18.1 In general [execpol.general]

[Contents](../README.md) · up: [20.18 Execution policies \[execpol\]](execpol.md) · prev: [20.18 Execution policies \[execpol\]](execpol.md) · next: [20.18.2 Header `<execution>` synopsis \[execution.syn\]](execution.syn.md)

**1** Subclause [[execpol]](execpol.md) describes classes that are *execution policy* types. An object of an execution policy type indicates the kinds of parallelism allowed in the execution of an algorithm and expresses the consequent requirements on the element access functions. [*Example 1*:

```cpp
using namespace std;
vector<int> v = /* ... */;

// standard sequential sort
sort(v.begin(), v.end());

// explicitly sequential sort
sort(execution::seq, v.begin(), v.end());

// permitting parallel execution
sort(execution::par, v.begin(), v.end());

// permitting vectorization as well
sort(execution::par_unseq, v.begin(), v.end());
```

— *end example*] [*Note 1*: Because different parallel architectures may require idiosyncratic parameters for efficient execution, implementations may provide additional execution policies to those described in this standard as extensions. — *end note*]
