# 32.5.3.1 In general [thread.mutex.requirements.general]

[Contents](../../README.md) · up: [32.5.3 Mutex requirements \[thread.mutex.requirements\]](thread.mutex.requirements.md) · prev: [32.5.3 Mutex requirements \[thread.mutex.requirements\]](thread.mutex.requirements.md) · next: [32.5.3.2 Mutex types \[thread.mutex.requirements.mutex\]](thread.mutex.requirements.mutex.md)

**1** A mutex object facilitates protection against data races and allows safe synchronization of data between execution agents ([[thread.req.lockable]](thread.req.lockable.md)). An execution agent *owns* a mutex from the time it successfully calls one of the lock functions until it calls unlock. Mutexes can be either recursive or non-recursive, and can grant simultaneous ownership to one or many execution agents. Both recursive and non-recursive mutexes are supplied.
