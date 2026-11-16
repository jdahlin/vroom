# 32.5 Mutual exclusion [thread.mutex]

[Contents](../../README.md) · up: [32 Thread support library \[thread\]](thread.md) · prev: [32.4.4 Namespace `this_thread` \[thread.thread.this\]](thread.thread.this.md) · next: [32.5.1 Header `<mutex>` synopsis \[mutex.syn\]](mutex.syn.md)

**1** This subclause provides mechanisms for mutual exclusion: mutexes, locks, and call once. These mechanisms ease the production of race-free programs ([[intro.multithread]](../../basic/intro.multithread.md)).

## Subclauses

- [32.5.1 Header `<mutex>` synopsis \[mutex.syn\]](mutex.syn.md)
- [32.5.2 Header `<shared_mutex>` synopsis \[shared.mutex.syn\]](shared.mutex.syn.md)
- [32.5.3 Mutex requirements \[thread.mutex.requirements\]](thread.mutex.requirements.md)
  - [32.5.3.1 In general \[thread.mutex.requirements.general\]](thread.mutex.requirements.general.md)
  - [32.5.3.2 Mutex types \[thread.mutex.requirements.mutex\]](thread.mutex.requirements.mutex.md)
    - [32.5.3.2.1 Class `mutex` \[thread.mutex.class\]](thread.mutex.class.md)
    - [32.5.3.2.2 Class `recursive_mutex` \[thread.mutex.recursive\]](thread.mutex.recursive.md)
  - [32.5.3.3 Timed mutex types \[thread.timedmutex.requirements\]](thread.timedmutex.requirements.md)
    - [32.5.3.3.1 Class `timed_mutex` \[thread.timedmutex.class\]](thread.timedmutex.class.md)
    - [32.5.3.3.2 Class `recursive_timed_mutex` \[thread.timedmutex.recursive\]](thread.timedmutex.recursive.md)
  - [32.5.3.4 Shared mutex types \[thread.sharedmutex.requirements\]](thread.sharedmutex.requirements.md)
    - [32.5.3.4.1 Class `shared_mutex` \[thread.sharedmutex.class\]](thread.sharedmutex.class.md)
  - [32.5.3.5 Shared timed mutex types \[thread.sharedtimedmutex.requirements\]](thread.sharedtimedmutex.requirements.md)
    - [32.5.3.5.1 Class `shared_timed_mutex` \[thread.sharedtimedmutex.class\]](thread.sharedtimedmutex.class.md)
- [32.5.4 Locks \[thread.lock\]](thread.lock.md)
  - [32.5.4.1 Class template `lock_guard` \[thread.lock.guard\]](thread.lock.guard.md)
  - [32.5.4.2 Class template `scoped_lock` \[thread.lock.scoped\]](thread.lock.scoped.md)
  - [32.5.4.3 Class template `unique_lock` \[thread.lock.unique\]](thread.lock.unique.md)
    - [32.5.4.3.1 Constructors, destructor, and assignment \[thread.lock.unique.cons\]](thread.lock.unique.cons.md)
    - [32.5.4.3.2 Locking \[thread.lock.unique.locking\]](thread.lock.unique.locking.md)
    - [32.5.4.3.3 Modifiers \[thread.lock.unique.mod\]](thread.lock.unique.mod.md)
    - [32.5.4.3.4 Observers \[thread.lock.unique.obs\]](thread.lock.unique.obs.md)
  - [32.5.4.4 Class template `shared_lock` \[thread.lock.shared\]](thread.lock.shared.md)
    - [32.5.4.4.1 Constructors, destructor, and assignment \[thread.lock.shared.cons\]](thread.lock.shared.cons.md)
    - [32.5.4.4.2 Locking \[thread.lock.shared.locking\]](thread.lock.shared.locking.md)
    - [32.5.4.4.3 Modifiers \[thread.lock.shared.mod\]](thread.lock.shared.mod.md)
    - [32.5.4.4.4 Observers \[thread.lock.shared.obs\]](thread.lock.shared.obs.md)
- [32.5.5 Generic locking algorithms \[thread.lock.algorithm\]](thread.lock.algorithm.md)
- [32.5.6 Call once \[thread.once\]](thread.once.md)
  - [32.5.6.1 Struct `once_flag` \[thread.once.onceflag\]](thread.once.onceflag.md)
  - [32.5.6.2 Function `call_once` \[thread.once.callonce\]](thread.once.callonce.md)
