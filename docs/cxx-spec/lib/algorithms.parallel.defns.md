# 25.3.1 Preamble [algorithms.parallel.defns]

[Contents](../README.md) · up: [25.3 Parallel algorithms \[algorithms.parallel\]](algorithms.parallel.md) · prev: [25.3 Parallel algorithms \[algorithms.parallel\]](algorithms.parallel.md) · next: [25.3.2 Requirements on user-provided function objects \[algorithms.parallel.user\]](algorithms.parallel.user.md)

**1** Subclause [[algorithms.parallel]](algorithms.parallel.md) describes components that C++ programs may use to perform operations on containers and other sequences in parallel.

**2** A *parallel algorithm* is a function template listed in this document with a template parameter named `ExecutionPolicy`.

**3** Parallel algorithms access objects indirectly accessible via their arguments by invoking the following functions:

- All operations of the categories of the iterators that the algorithm is instantiated with.
- Operations on those sequence elements that are required by its specification.
- User-provided function objects to be applied during the execution of the algorithm, if required by the specification.
- Operations on those function objects required by the specification. [*Note 1*: See [[algorithms.requirements]](algorithms.requirements.md). — *end note*]

These functions are herein called *element access functions*. [*Example 1*: The `sort` function may invoke the following element access functions:

- Operations of the random-access iterator of the actual template argument (as per [[random.access.iterators]](random.access.iterators.md)), as implied by the name of the template parameter `RandomAccessIterator`.
- The `swap` function on the elements of the sequence (as per the preconditions specified in [[sort]](sort.md)).
- The user-provided `Compare` function object.

— *end example*]

**4** A standard library function is *vectorization-unsafe* if it is specified to synchronize with another function invocation, or another function invocation is specified to synchronize with it, and if it is not a memory allocation or deallocation function. [*Note 2*: Implementations must ensure that internal synchronization inside standard library functions does not prevent forward progress when those functions are executed by threads of execution with weakly parallel forward progress guarantees. — *end note*] [*Example 2*:

```cpp
int x = 0;
std::mutex m;
void f() {
  int a[] = {1,2};
  std::for_each(std::execution::par_unseq, std::begin(a), std::end(a), [&](int) {
    std::lock_guard<mutex> guard(m);            // incorrect: lock_guard constructor calls m.lock()
  ++x;
  });
}
```

The above program may result in two consecutive calls to `m.lock()` on the same thread of execution (which may deadlock), because the applications of the function object are not guaranteed to run on different threads of execution. — *end example*]
