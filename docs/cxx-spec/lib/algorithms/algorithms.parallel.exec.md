# 25.3.3 Effect of execution policies on algorithm execution [algorithms.parallel.exec]

[Contents](../../README.md) · up: [25.3 Parallel algorithms \[algorithms.parallel\]](algorithms.parallel.md) · prev: [25.3.2 Requirements on user-provided function objects \[algorithms.parallel.user\]](algorithms.parallel.user.md) · next: [25.3.4 Parallel algorithm exceptions \[algorithms.parallel.exceptions\]](algorithms.parallel.exceptions.md)

**1** Parallel algorithms have template parameters named `ExecutionPolicy` ([[execpol]](../utilities/execpol.md)) which describe the manner in which the execution of these algorithms may be parallelized and the manner in which they apply the element access functions.

**2** If an object is modified by an element access function, the algorithm will perform no other unsynchronized accesses to that object. The modifying element access functions are those which are specified as modifying the object. [*Note 1*: For example, `swap`, `++`, `--`, `@=`, and assignments modify the object. For the assignment and `@=` operators, only the left argument is modified. — *end note*]

**3** Unless otherwise stated, implementations may make arbitrary copies of elements (with type `T`) from sequences where `is_trivially_copy_constructible_v<T>` and `is_trivially_destructible_v<T>` are `true`. [*Note 2*: This implies that user-supplied function objects should not rely on object identity of arguments for such input sequences. Users for whom the object identity of the arguments to these function objects is important should consider using a wrapping iterator that returns a non-copied implementation object such as `reference_wrapper<T>` ([[refwrap]](../utilities/refwrap.md)) or some equivalent solution. — *end note*]

**4** The invocations of element access functions in parallel algorithms invoked with an execution policy object of type `execution::sequenced_policy` all occur in the calling thread of execution. [*Note 3*: The invocations are not interleaved; see [[intro.execution]](../../basic/intro.execution.md). — *end note*]

**5** The invocations of element access functions in parallel algorithms invoked with an execution policy object of type `execution::unsequenced_policy` are permitted to execute in an unordered fashion in the calling thread of execution, unsequenced with respect to one another in the calling thread of execution. [*Note 4*: This means that multiple function object invocations may be interleaved on a single thread of execution, which overrides the usual guarantee from [[intro.execution]](../../basic/intro.execution.md) that function executions do not overlap with one another. — *end note*] The behavior of a program is undefined if it invokes a vectorization-unsafe standard library function from user code called from a `execution::unsequenced_policy` algorithm. [*Note 5*: Because `execution::unsequenced_policy` allows the execution of element access functions to be interleaved on a single thread of execution, blocking synchronization, including the use of mutexes, risks deadlock. — *end note*]

**6** The invocations of element access functions in parallel algorithms invoked with an execution policy object of type `execution::parallel_policy` are permitted to execute either in the invoking thread of execution or in a thread of execution implicitly created by the library to support parallel algorithm execution. If the threads of execution created by `thread` ([[thread.thread.class]](../thread/thread.thread.class.md)) or `jthread` ([[thread.jthread.class]](../thread/thread.jthread.class.md)) provide concurrent forward progress guarantees ([[intro.progress]](../../basic/intro.progress.md)), then a thread of execution implicitly created by the library will provide parallel forward progress guarantees; otherwise, the provided forward progress guarantee is implementation-defined. Any such invocations executing in the same thread of execution are indeterminately sequenced with respect to each other. [*Note 6*: It is the caller’s responsibility to ensure that the invocation does not introduce data races or deadlocks. — *end note*] [*Example 1*:

```cpp
int a[] = {0,1};
std::vector<int> v;
std::for_each(std::execution::par, std::begin(a), std::end(a), [&](int i) {
  v.push_back(i*2+1);                   // incorrect: data race
});
```

The program above has a data race because of the unsynchronized access to the container `v`. — *end example*] [*Example 2*:

```cpp
std::atomic<int> x{0};
int a[] = {1,2};
std::for_each(std::execution::par, std::begin(a), std::end(a), [&](int) {
  x.fetch_add(1, std::memory_order::relaxed);
  // spin wait for another iteration to change the value of x
  while (x.load(std::memory_order::relaxed) == 1) { }           // incorrect: assumes execution order
});
```

The above example depends on the order of execution of the iterations, and will not terminate if both iterations are executed sequentially on the same thread of execution. — *end example*] [*Example 3*:

```cpp
int x = 0;
std::mutex m;
int a[] = {1,2};
std::for_each(std::execution::par, std::begin(a), std::end(a), [&](int) {
  std::lock_guard<mutex> guard(m);
  ++x;
});
```

The above example synchronizes access to object `x` ensuring that it is incremented correctly. — *end example*]

**7** The invocations of element access functions in parallel algorithms invoked with an execution policy object of type `execution::parallel_unsequenced_policy` are permitted to execute in an unordered fashion in unspecified threads of execution, and unsequenced with respect to one another within each thread of execution. These threads of execution are either the invoking thread of execution or threads of execution implicitly created by the library; the latter will provide weakly parallel forward progress guarantees. [*Note 7*: This means that multiple function object invocations may be interleaved on a single thread of execution, which overrides the usual guarantee from [[intro.execution]](../../basic/intro.execution.md) that function executions do not overlap with one another. — *end note*] The behavior of a program is undefined if it invokes a vectorization-unsafe standard library function from user code called from a `execution::parallel_unsequenced_policy` algorithm. [*Note 8*: Because `execution::parallel_unsequenced_policy` allows the execution of element access functions to be interleaved on a single thread of execution, blocking synchronization, including the use of mutexes, risks deadlock. — *end note*]

**8** [*Note 9*: The semantics of invocation with `execution::unsequenced_policy`, `execution::parallel_policy`, or `execution::parallel_unsequenced_policy` allow the implementation to fall back to sequential execution if the system cannot parallelize an algorithm invocation, e.g., due to lack of resources. — *end note*]

**9** If an invocation of a parallel algorithm uses threads of execution implicitly created by the library, then the invoking thread of execution will either

- temporarily block with forward progress guarantee delegation ([[intro.progress]](../../basic/intro.progress.md)) on the completion of these library-managed threads of execution, or
- eventually execute an element access function;

the thread of execution will continue to do so until the algorithm is finished. [*Note 10*: In blocking with forward progress guarantee delegation in this context, a thread of execution created by the library is considered to have finished execution as soon as it has finished the execution of the particular element access function that the invoking thread of execution logically depends on. — *end note*]

**10** The semantics of parallel algorithms invoked with an execution policy object of implementation-defined type are implementation-defined.
