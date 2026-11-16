# 32.9.5 Shared state [futures.state]

[Contents](../README.md) · up: [32.9 Futures \[futures\]](futures.md) · prev: [32.9.4 Class `future_error` \[futures.future.error\]](futures.future.error.md) · next: [32.9.6 Class template `promise` \[futures.promise\]](futures.promise.md)

**1** Many of the classes introduced in subclause [[futures]](futures.md) use some state to communicate results. This *shared state* consists of some state information and some (possibly not yet evaluated) *result*, which can be a (possibly void) value or an exception. [*Note 1*: Futures, promises, and tasks defined in this clause reference such shared state. — *end note*]

**2** [*Note 2*: The result can be any kind of object including a function to compute that result, as used by `async` when `policy` is `launch::deferred`. — *end note*]

**3** An *asynchronous return object* is an object that reads results from a shared state. A *waiting function* of an asynchronous return object is one that potentially blocks to wait for the shared state to be made ready. If a waiting function can return before the state is made ready because of a timeout ([[thread.req.lockable]](thread.req.lockable.md)), then it is a *timed waiting function*, otherwise it is a *non-timed waiting function*.

**4** An *asynchronous provider* is an object that provides a result to a shared state. The result of a shared state is set by respective functions on the asynchronous provider. [*Note 3*: Such as promises or tasks. — *end note*] The means of setting the result of a shared state is specified in the description of those classes and functions that create such a state object.

**5** When an asynchronous return object or an asynchronous provider is said to release its shared state, it means:

- if the return object or provider holds the last reference to its shared state, the shared state is destroyed; and
- the return object or provider gives up its reference to its shared state; and
- these actions will not block for the shared state to become ready, except that it may block if all of the following are true: the shared state was created by a call to `std::async`, the shared state is not yet ready, and this was the last reference to the shared state.

**6** When an asynchronous provider is said to make its shared state ready, it means:

- first, the provider marks its shared state as ready; and
- second, the provider unblocks any execution agents waiting for its shared state to become ready.

**7** When an asynchronous provider is said to abandon its shared state, it means:

- first, if that state is not ready, the provider

  - stores an exception object of type `future_error` with an error condition of `broken_promise` within its shared state; and then
  - makes its shared state ready;

- second, the provider releases its shared state.

**8** A shared state is *ready* only if it holds a value or an exception ready for retrieval. Waiting for a shared state to become ready may invoke code to compute the result on the waiting thread if so specified in the description of the class or function that creates the state object.

**9** Calls to functions that successfully set the stored result of a shared state synchronize with ([[intro.multithread]](../intro.multithread.md)) calls to functions successfully detecting the ready state resulting from that setting. The storage of the result (whether normal or exceptional) into the shared state synchronizes with ([[intro.multithread]](../intro.multithread.md)) the successful return from a call to a waiting function on the shared state.

**10** Some functions (e.g., `promise::set_value_at_thread_exit`) delay making the shared state ready until the calling thread exits. The destruction of each of that thread’s objects with thread storage duration ([[basic.stc.thread]](../basic.stc.thread.md)) is sequenced before making that shared state ready.

**11** Access to the result of the same shared state may conflict ([[intro.multithread]](../intro.multithread.md)). [*Note 4*: This explicitly specifies that the result of the shared state is visible in the objects that reference this state in the sense of data race avoidance ([[res.on.data.races]](res.on.data.races.md)). For example, concurrent accesses through references returned by `shared_future::get()` ([[futures.shared.future]](futures.shared.future.md)) must either use read-only operations or provide additional synchronization. — *end note*]
