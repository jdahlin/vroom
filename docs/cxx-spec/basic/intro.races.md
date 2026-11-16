# 6.9.2.1 Data races [intro.races]

[Contents](../README.md) · up: [6.9.2 Multi-threaded executions and data races \[intro.multithread\]](intro.multithread.md) · prev: [6.9.2 Multi-threaded executions and data races \[intro.multithread\]](intro.multithread.md) · next: [6.9.2.2 Forward progress \[intro.progress\]](intro.progress.md)

**1** The value of an object visible to a thread *T* at a particular point is the initial value of the object, a value assigned to the object by *T*, or a value assigned to the object by another thread, according to the rules below. [*Note 1*: In some cases, there may instead be undefined behavior. Much of this subclause is motivated by the desire to support atomic operations with explicit and detailed visibility constraints. However, it also implicitly supports a simpler view for more restricted programs. — *end note*]

**2** Two expression evaluations *conflict* if one of them modifies a memory location ([[intro.memory]](intro.memory.md)) and the other one reads or modifies the same memory location.

**3** The library defines a number of atomic operations ([[atomics]](../lib/atomics/atomics.md)) and operations on mutexes ([[thread]](../lib/thread/thread.md)) that are specially identified as synchronization operations. These operations play a special role in making assignments in one thread visible to another. A synchronization operation on one or more memory locations is either a consume operation, an acquire operation, a release operation, or both an acquire and release operation. A synchronization operation without an associated memory location is a fence and can be either an acquire fence, a release fence, or both an acquire and release fence. In addition, there are relaxed atomic operations, which are not synchronization operations, and atomic read-modify-write operations, which have special characteristics. [*Note 2*: For example, a call that acquires a mutex will perform an acquire operation on the locations comprising the mutex. Correspondingly, a call that releases the same mutex will perform a release operation on those same locations. Informally, performing a release operation on *A* forces prior side effects on other memory locations to become visible to other threads that later perform a consume or an acquire operation on *A*. “Relaxed” atomic operations are not synchronization operations even though, like synchronization operations, they cannot contribute to data races. — *end note*]

**4** All modifications to a particular atomic object *M* occur in some particular total order, called the *modification order* of *M*. [*Note 3*: There is a separate order for each atomic object. There is no requirement that these can be combined into a single total order for all objects. In general this will be impossible since different threads may observe modifications to different objects in inconsistent orders. — *end note*]

**5** A *release sequence* headed by a release operation *A* on an atomic object *M* is a maximal contiguous sub-sequence of side effects in the modification order of *M*, where the first operation is *A*, and every subsequent operation is an atomic read-modify-write operation.

**6** Certain library calls *synchronize with* other library calls performed by another thread. For example, an atomic store-release synchronizes with a load-acquire that takes its value from the store ([[atomics.order]](../lib/atomics/atomics.order.md)). [*Note 4*: Except in the specified cases, reading a later value does not necessarily ensure visibility as described below. Such a requirement would sometimes interfere with efficient implementation. — *end note*] [*Note 5*: The specifications of the synchronization operations define when one reads the value written by another. For atomic objects, the definition is clear. All operations on a given mutex occur in a single total order. Each mutex acquisition “reads the value written” by the last mutex release. — *end note*]

**7** An evaluation *A* *carries a dependency* to an evaluation *B* if

- the value of *A* is used as an operand of *B*, unless:

  - *B* is an invocation of any specialization of `std::kill_dependency` ([[atomics.order]](../lib/atomics/atomics.order.md)), or
  - *A* is the left operand of a built-in logical AND (`&&`, see [[expr.log.and]](../expr/expr.log.and.md)) or logical OR (`||`, see [[expr.log.or]](../expr/expr.log.or.md)) operator, or
  - *A* is the left operand of a conditional (`?:`, see [[expr.cond]](../expr/expr.cond.md)) operator, or
  - *A* is the left operand of the built-in comma (`,`) operator ([[expr.comma]](../expr/expr.comma.md));

  or

- *A* writes a scalar object or bit-field *M*, *B* reads the value written by *A* from *M*, and *A* is sequenced before *B*, or

- for some evaluation *X*, *A* carries a dependency to *X*, and *X* carries a dependency to *B*.

[*Note 6*: “Carries a dependency to” is a subset of “is sequenced before”, and is similarly strictly intra-thread. — *end note*]

**8** An evaluation *A* is *dependency-ordered before* an evaluation *B* if

- *A* performs a release operation on an atomic object *M*, and, in another thread, *B* performs a consume operation on *M* and reads the value written by *A*, or
- for some evaluation *X*, *A* is dependency-ordered before *X* and *X* carries a dependency to *B*.

[*Note 7*: The relation “is dependency-ordered before” is analogous to “synchronizes with”, but uses release/consume in place of release/acquire. — *end note*]

**9** An evaluation *A* *inter-thread happens before* an evaluation *B* if

- *A* synchronizes with *B*, or

- *A* is dependency-ordered before *B*, or

- for some evaluation *X*

  - *A* synchronizes with *X* and *X* is sequenced before *B*, or
  - *A* is sequenced before *X* and *X* inter-thread happens before *B*, or
  - *A* inter-thread happens before *X* and *X* inter-thread happens before *B*.

[*Note 8*: The “inter-thread happens before” relation describes arbitrary concatenations of “sequenced before”, “synchronizes with” and “dependency-ordered before” relationships, with two exceptions. The first exception is that a concatenation is not permitted to end with “dependency-ordered before” followed by “sequenced before”. The reason for this limitation is that a consume operation participating in a “dependency-ordered before” relationship provides ordering only with respect to operations to which this consume operation actually carries a dependency. The reason that this limitation applies only to the end of such a concatenation is that any subsequent release operation will provide the required ordering for a prior consume operation. The second exception is that a concatenation is not permitted to consist entirely of “sequenced before”. The reasons for this limitation are (1) to permit “inter-thread happens before” to be transitively closed and (2) the “happens before” relation, defined below, provides for relationships consisting entirely of “sequenced before”. — *end note*]

**10** An evaluation *A* *happens before* an evaluation *B* (or, equivalently, *B* *happens after* *A*) if:

- *A* is sequenced before *B*, or
- *A* inter-thread happens before *B*.

The implementation shall ensure that no program execution demonstrates a cycle in the “happens before” relation. [*Note 9*: This cycle would otherwise be possible only through the use of consume operations. — *end note*]

**11** An evaluation *A* *simply happens before* an evaluation *B* if either

- *A* is sequenced before *B*, or
- *A* synchronizes with *B*, or
- *A* simply happens before *X* and *X* simply happens before *B*.

[*Note 10*: In the absence of consume operations, the happens before and simply happens before relations are identical. — *end note*]

**12** An evaluation *A* *strongly happens before* an evaluation *D* if, either

- *A* is sequenced before *D*, or
- *A* synchronizes with *D*, and both *A* and *D* are sequentially consistent atomic operations ([[atomics.order]](../lib/atomics/atomics.order.md)), or
- there are evaluations *B* and *C* such that *A* is sequenced before *B*, *B* simply happens before *C*, and *C* is sequenced before *D*, or
- there is an evaluation *B* such that *A* strongly happens before *B*, and *B* strongly happens before *D*.

[*Note 11*: Informally, if *A* strongly happens before *B*, then *A* appears to be evaluated before *B* in all contexts. Strongly happens before excludes consume operations. — *end note*]

**13** A *visible side effect* *A* on a scalar object or bit-field *M* with respect to a value computation *B* of *M* satisfies the conditions:

- *A* happens before *B* and
- there is no other side effect *X* to *M* such that *A* happens before *X* and *X* happens before *B*.

The value of a non-atomic scalar object or bit-field *M*, as determined by evaluation *B*, shall be the value stored by the visible side effect *A*. [*Note 12*: If there is ambiguity about which side effect to a non-atomic object or bit-field is visible, then the behavior is either unspecified or undefined. — *end note*] [*Note 13*: This states that operations on ordinary objects are not visibly reordered. This is not actually detectable without data races, but it is necessary to ensure that data races, as defined below, and with suitable restrictions on the use of atomics, correspond to data races in a simple interleaved (sequentially consistent) execution. — *end note*]

**14** The value of an atomic object *M*, as determined by evaluation *B*, shall be the value stored by some side effect *A* that modifies *M*, where *B* does not happen before *A*. [*Note 14*: The set of such side effects is also restricted by the rest of the rules described here, and in particular, by the coherence requirements below. — *end note*]

**15** If an operation *A* that modifies an atomic object *M* happens before an operation *B* that modifies *M*, then *A* shall be earlier than *B* in the modification order of *M*. [*Note 15*: This requirement is known as write-write coherence. — *end note*]

**16** If a value computation *A* of an atomic object *M* happens before a value computation *B* of *M*, and *A* takes its value from a side effect *X* on *M*, then the value computed by *B* shall either be the value stored by *X* or the value stored by a side effect *Y* on *M*, where *Y* follows *X* in the modification order of *M*. [*Note 16*: This requirement is known as read-read coherence. — *end note*]

**17** If a value computation *A* of an atomic object *M* happens before an operation *B* that modifies *M*, then *A* shall take its value from a side effect *X* on *M*, where *X* precedes *B* in the modification order of *M*. [*Note 17*: This requirement is known as read-write coherence. — *end note*]

**18** If a side effect *X* on an atomic object *M* happens before a value computation *B* of *M*, then the evaluation *B* shall take its value from *X* or from a side effect *Y* that follows *X* in the modification order of *M*. [*Note 18*: This requirement is known as write-read coherence. — *end note*]

**19** [*Note 19*: The four preceding coherence requirements effectively disallow compiler reordering of atomic operations to a single object, even if both operations are relaxed loads. This effectively makes the cache coherence guarantee provided by most hardware available to C++ atomic operations. — *end note*]

**20** [*Note 20*: The value observed by a load of an atomic depends on the “happens before” relation, which depends on the values observed by loads of atomics. The intended reading is that there must exist an association of atomic loads with modifications they observe that, together with suitably chosen modification orders and the “happens before” relation derived as described above, satisfy the resulting constraints as imposed here. — *end note*]

**21** Two actions are *potentially concurrent* if

- they are performed by different threads, or
- they are unsequenced, at least one is performed by a signal handler, and they are not both performed by the same signal handler invocation.

The execution of a program contains a *data race* if it contains two potentially concurrent conflicting actions, at least one of which is not atomic, and neither happens before the other, except for the special case for signal handlers described below. Any such data race results in undefined behavior. [*Note 21*: It can be shown that programs that correctly use mutexes and `memory_order::seq_cst` operations to prevent all data races and use no other synchronization operations behave as if the operations executed by their constituent threads were simply interleaved, with each value computation of an object being taken from the last side effect on that object in that interleaving. This is normally referred to as “sequential consistency”. However, this applies only to data-race-free programs, and data-race-free programs cannot observe most program transformations that do not change single-threaded program semantics. In fact, most single-threaded program transformations continue to be allowed, since any program that behaves differently as a result must perform an undefined operation. — *end note*]

**22** Two accesses to the same object of type `volatile std::sig_atomic_t` do not result in a data race if both occur in the same thread, even if one or more occurs in a signal handler. For each signal handler invocation, evaluations performed by the thread invoking a signal handler can be divided into two groups *A* and *B*, such that no evaluations in *B* happen before evaluations in *A*, and the evaluations of such `volatile std::sig_atomic_t` objects take values as though all evaluations in *A* happened before the execution of the signal handler and the execution of the signal handler happened before all evaluations in *B*.

**23** [*Note 22*: Compiler transformations that introduce assignments to a potentially shared memory location that would not be modified by the abstract machine are generally precluded by this document, since such an assignment might overwrite another assignment by a different thread in cases in which an abstract machine execution would not have encountered a data race. This includes implementations of data member assignment that overwrite adjacent members in separate memory locations. Reordering of atomic loads in cases in which the atomics in question may alias is also generally precluded, since this may violate the coherence rules. — *end note*]

**24** [*Note 23*: Transformations that introduce a speculative read of a potentially shared memory location may not preserve the semantics of the C++ program as defined in this document, since they potentially introduce a data race. However, they are typically valid in the context of an optimizing compiler that targets a specific machine with well-defined semantics for data races. They would be invalid for a hypothetical machine that is not tolerant of races or provides hardware race detection. — *end note*]
