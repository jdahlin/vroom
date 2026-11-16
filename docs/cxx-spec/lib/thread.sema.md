# 32.7 Semaphore [thread.sema]

[Contents](../README.md) · up: [32 Thread support library \[thread\]](thread.md) · prev: [32.6.4.2 Interruptible waits \[thread.condvarany.intwait\]](thread.condvarany.intwait.md) · next: [32.7.1 Header `<semaphore>` synopsis \[semaphore.syn\]](semaphore.syn.md)

**1** Semaphores are lightweight synchronization primitives used to constrain concurrent access to a shared resource. They are widely used to implement other synchronization primitives and, whenever both are applicable, can be more efficient than condition variables.

**2** A counting semaphore is a semaphore object that models a non-negative resource count. A binary semaphore is a semaphore object that has only two states. A binary semaphore should be more efficient than the default implementation of a counting semaphore with a unit resource count.

## Subclauses

- [32.7.1 Header `<semaphore>` synopsis \[semaphore.syn\]](semaphore.syn.md)
- [32.7.2 Class template `counting_semaphore` \[thread.sema.cnt\]](thread.sema.cnt.md)
