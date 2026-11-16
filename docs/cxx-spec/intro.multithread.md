# 6.9.2 Multi-threaded executions and data races [intro.multithread]

[Contents](README.md) · up: [6.9 Program execution \[basic.exec\]](basic.exec.md) · prev: [6.9.1 Sequential execution \[intro.execution\]](intro.execution.md) · next: [6.9.2.1 Data races \[intro.races\]](intro.races.md)

**1** A *thread of execution* (also known as a *thread*) is a single flow of control within a program, including the initial invocation of a specific top-level function, and recursively including every function invocation subsequently executed by the thread. [*Note 1*: When one thread creates another, the initial call to the top-level function of the new thread is executed by the new thread, not by the creating thread. — *end note*] Every thread in a program can potentially access every object and function in a program.[^1] Under a hosted implementation, a C++ program can have more than one thread running concurrently. The execution of each thread proceeds as defined by the remainder of this document. The execution of the entire program consists of an execution of all of its threads. [*Note 2*: Usually the execution can be viewed as an interleaving of all its threads. However, some kinds of atomic operations, for example, allow executions inconsistent with a simple interleaving, as described below. — *end note*] Under a freestanding implementation, it is implementation-defined whether a program can have more than one thread of execution.

**2** For a signal handler that is not executed as a result of a call to the `std::raise` function, it is unspecified which thread of execution contains the signal handler invocation.

## Subclauses

- [6.9.2.1 Data races \[intro.races\]](intro.races.md)
- [6.9.2.2 Forward progress \[intro.progress\]](intro.progress.md)

[^1]: An object with automatic or thread storage duration ([[basic.stc]](basic.stc.md)) is associated with one specific thread, and can be accessed by a different thread only indirectly through a pointer or reference ([[basic.compound]](basic.compound.md)).
