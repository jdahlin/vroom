# 32.8.2 Barriers [thread.barrier]

[Contents](../README.md) · up: [32.8 Coordination types \[thread.coord\]](thread.coord.md) · prev: [32.8.1.2 Class `latch` \[thread.latch.class\]](thread.latch.class.md) · next: [32.8.2.1 Header `<barrier>` synopsis \[barrier.syn\]](barrier.syn.md)

**1** A barrier is a thread coordination mechanism whose lifetime consists of a sequence of barrier phases, where each phase allows at most an expected number of threads to block until the expected number of threads arrive at the barrier. [*Note 1*: A barrier is useful for managing repeated tasks that are handled by multiple threads. — *end note*]

## Subclauses

- [32.8.2.1 Header `<barrier>` synopsis \[barrier.syn\]](barrier.syn.md)
- [32.8.2.2 Class template `barrier` \[thread.barrier.class\]](thread.barrier.class.md)
