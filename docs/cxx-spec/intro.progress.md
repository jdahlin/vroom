# 6.9.2.2 Forward progress [intro.progress]

[Contents](README.md) · up: [6.9.2 Multi-threaded executions and data races \[intro.multithread\]](intro.multithread.md) · prev: [6.9.2.1 Data races \[intro.races\]](intro.races.md) · next: [6.9.3 Start and termination \[basic.start\]](basic.start.md)

**1** The implementation may assume that any thread will eventually do one of the following:

- terminate,
- make a call to a library I/O function,
- perform an access through a volatile glvalue, or
- perform a synchronization operation or an atomic operation.

[*Note 1*: This is intended to allow compiler transformations such as removal of empty loops, even when termination cannot be proven. — *end note*]

**2** Executions of atomic functions that are either defined to be lock-free ([[atomics.flag]](lib/atomics.flag.md)) or indicated as lock-free ([[atomics.lockfree]](lib/atomics.lockfree.md)) are *lock-free executions*.

- If there is only one thread that is not blocked ([[defns.block]](intro.defs.md#defns.block)) in a standard library function, a lock-free execution in that thread shall complete. [*Note 2*: Concurrently executing threads may prevent progress of a lock-free execution. For example, this situation can occur with load-locked store-conditional implementations. This property is sometimes termed obstruction-free. — *end note*]
- When one or more lock-free executions run concurrently, at least one should complete. [*Note 3*: It is difficult for some implementations to provide absolute guarantees to this effect, since repeated and particularly inopportune interference from other threads may prevent forward progress, e.g., by repeatedly stealing a cache line for unrelated purposes between load-locked and store-conditional instructions. Implementations should ensure that such effects cannot indefinitely delay progress under expected operating conditions, and that such anomalies can therefore safely be ignored by programmers. Outside this document, this property is sometimes termed lock-free. — *end note*]

**3** During the execution of a thread of execution, each of the following is termed an *execution step*:

- termination of the thread of execution,
- performing an access through a volatile glvalue, or
- completion of a call to a library I/O function, a synchronization operation, or an atomic operation.

**4** An invocation of a standard library function that blocks ([[defns.block]](intro.defs.md#defns.block)) is considered to continuously execute execution steps while waiting for the condition that it blocks on to be satisfied. [*Example 1*: A library I/O function that blocks until the I/O operation is complete can be considered to continuously check whether the operation is complete. Each such check might consist of one or more execution steps, for example using observable behavior of the abstract machine. — *end example*]

**5** [*Note 4*: Because of this and the preceding requirement regarding what threads of execution have to perform eventually, it follows that no thread of execution can execute forever without an execution step occurring. — *end note*]

**6** A thread of execution *makes progress* when an execution step occurs or a lock-free execution does not complete because there are other concurrent threads that are not blocked in a standard library function (see above).

**7** For a thread of execution providing *concurrent forward progress guarantees*, the implementation ensures that the thread will eventually make progress for as long as it has not terminated. [*Note 5*: This is required regardless of whether or not other threads of executions (if any) have been or are making progress. To eventually fulfill this requirement means that this will happen in an unspecified but finite amount of time. — *end note*]

**8** It is implementation-defined whether the implementation-created thread of execution that executes `main` ([[basic.start.main]](basic.start.main.md)) and the threads of execution created by `std::thread` ([[thread.thread.class]](lib/thread.thread.class.md)) or `std::jthread` ([[thread.jthread.class]](lib/thread.jthread.class.md)) provide concurrent forward progress guarantees. [*Note 6*: General-purpose implementations should provide these guarantees. — *end note*]

**9** For a thread of execution providing *parallel forward progress guarantees*, the implementation is not required to ensure that the thread will eventually make progress if it has not yet executed any execution step; once this thread has executed a step, it provides concurrent forward progress guarantees.

**10** [*Note 7*: This does not specify a requirement for when to start this thread of execution, which will typically be specified by the entity that creates this thread of execution. For example, a thread of execution that provides concurrent forward progress guarantees and executes tasks from a set of tasks in an arbitrary order, one after the other, satisfies the requirements of parallel forward progress for these tasks. — *end note*]

**11** For a thread of execution providing *weakly parallel forward progress guarantees*, the implementation does not ensure that the thread will eventually make progress.

**12** [*Note 8*: Threads of execution providing weakly parallel forward progress guarantees cannot be expected to make progress regardless of whether other threads make progress or not; however, blocking with forward progress guarantee delegation, as defined below, can be used to ensure that such threads of execution make progress eventually. — *end note*]

**13** Concurrent forward progress guarantees are stronger than parallel forward progress guarantees, which in turn are stronger than weakly parallel forward progress guarantees. [*Note 9*: For example, some kinds of synchronization between threads of execution may only make progress if the respective threads of execution provide parallel forward progress guarantees, but will fail to make progress under weakly parallel guarantees. — *end note*]

**14** When a thread of execution *P* is specified to *block with forward progress guarantee delegation* on the completion of a set *S* of threads of execution, then throughout the whole time of *P* being blocked on *S*, the implementation shall ensure that the forward progress guarantees provided by at least one thread of execution in *S* is at least as strong as *P*’s forward progress guarantees. [*Note 10*: It is unspecified which thread or threads of execution in *S* are chosen and for which number of execution steps. The strengthening is not permanent and not necessarily in place for the rest of the lifetime of the affected thread of execution. As long as *P* is blocked, the implementation has to eventually select and potentially strengthen a thread of execution in *S*. — *end note*] Once a thread of execution in *S* terminates, it is removed from *S*. Once *S* is empty, *P* is unblocked.

**15** [*Note 11*: A thread of execution *B* thus can temporarily provide an effectively stronger forward progress guarantee for a certain amount of time, due to a second thread of execution *A* being blocked on it with forward progress guarantee delegation. In turn, if *B* then blocks with forward progress guarantee delegation on *C*, this may also temporarily provide a stronger forward progress guarantee to *C*. — *end note*]

**16** [*Note 12*: If all threads of execution in *S* finish executing (e.g., they terminate and do not use blocking synchronization incorrectly), then *P*’s execution of the operation that blocks with forward progress guarantee delegation will not result in *P*’s progress guarantee being effectively weakened. — *end note*]

**17** [*Note 13*: This does not remove any constraints regarding blocking synchronization for threads of execution providing parallel or weakly parallel forward progress guarantees because the implementation is not required to strengthen a particular thread of execution whose too-weak progress guarantee is preventing overall progress. — *end note*]

**18** An implementation should ensure that the last value (in modification order) assigned by an atomic or synchronization operation will become visible to all other threads in a finite period of time.
