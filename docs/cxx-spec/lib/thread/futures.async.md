# 32.9.9 Function template `async` [futures.async]

[Contents](../../README.md) · up: [32.9 Futures \[futures\]](futures.md) · prev: [32.9.8 Class template `shared_future` \[futures.shared.future\]](futures.shared.future.md) · next: [32.9.10 Class template `packaged_task` \[futures.task\]](futures.task.md)

**1** The function template `async` provides a mechanism to launch a function potentially in a new thread and provides the result of the function in a `future` object with which it shares a shared state.

```cpp
template<class F, class... Args>
  [[nodiscard]] future<invoke_result_t<decay_t<F>, decay_t<Args>...>>
    async(F&& f, Args&&... args);
template<class F, class... Args>
  [[nodiscard]] future<invoke_result_t<decay_t<F>, decay_t<Args>...>>
    async(launch policy, F&& f, Args&&... args);
```

**2** *Mandates:* The following are all `true`:

- `is_constructible_v<decay_t<F>, F>`,
- `(is_constructible_v<decay_t<Args>, Args> &&...)`,
- `is_move_constructible_v<decay_t<F>>`,
- `(is_move_constructible_v<decay_t<Args>> &&...)`, and
- `is_invocable_v<decay_t<F>, decay_t<Args>...>`.

**3** *Preconditions:* `decay_t<F>` and each type in `decay_t<Args>` meet the *Cpp17MoveConstructible* requirements.

**4** *Effects:* The first function behaves the same as a call to the second function with a `policy` argument of `launch::async | launch::deferred` and the same arguments for `F` and `Args`. The second function creates a shared state that is associated with the returned `future` object. The further behavior of the second function depends on the `policy` argument as follows (if more than one of these conditions applies, the implementation may choose any of the corresponding policies):

- If `launch::async` is set in `policy`, calls `invoke(decay-copy(std::forward<F>(f)),` `decay-copy(std::forward<Args>(args))...)` ([[func.require]](../utilities/func.require.md), [[thread.thread.constr]](thread.thread.constr.md)) as if in a new thread of execution represented by a `thread` object with the calls to `decay-copy` being evaluated in the thread that called `async`. Any return value is stored as the result in the shared state. Any exception propagated from the execution of `invoke(decay-copy(std::forward<F>(f)), decay-copy(std::forward<Args>(args))...)` is stored as the exceptional result in the shared state. The `thread` object is stored in the shared state and affects the behavior of any asynchronous return objects that reference that state.
- If `launch::deferred` is set in `policy`, stores `decay-copy(std::forward<F>(f))` and `decay-copy(std::forward<Args>(args))...` in the shared state. These copies of `f` and `args` constitute a *deferred function*. Invocation of the deferred function evaluates `invoke(std::move(g), std::move(xyz))` where `g` is the stored value of `decay-copy(std::forward<F>(f))` and `xyz` is the stored copy of `decay-copy(std::forward<Args>(args))...`. Any return value is stored as the result in the shared state. Any exception propagated from the execution of the deferred function is stored as the exceptional result in the shared state. The shared state is not made ready until the function has completed. The first call to a non-timed waiting function ([[futures.state]](futures.state.md)) on an asynchronous return object referring to this shared state invokes the deferred function in the thread that called the waiting function. Once evaluation of `invoke(std::move(g), std::move(xyz))` begins, the function is no longer considered deferred. [*Note 1*: If this policy is specified together with other policies, such as when using a `policy` value of `launch::async | launch::deferred`, implementations should defer invocation or the selection of the policy when no more concurrency can be effectively exploited. — *end note*]
- If no value is set in the launch policy, or a value is set that is neither specified in this document nor by the implementation, the behavior is undefined.

**5** *Returns:* An object of type `future<invoke_result_t<decay_t<F>, decay_t<Args>...>>` that refers to the shared state created by this call to `async`. [*Note 2*: If a future obtained from `async` is moved outside the local scope, other code that uses the future should be aware that the future’s destructor can block for the shared state to become ready. — *end note*]

**6** *Synchronization:* Regardless of the provided `policy` argument,

- the invocation of `async` synchronizes with ([[intro.multithread]](../../basic/intro.multithread.md)) the invocation of `f`. [*Note 3*: This statement applies even when the corresponding `future` object is moved to another thread. — *end note*]; and
- the completion of the function `f` is sequenced before ([[intro.multithread]](../../basic/intro.multithread.md)) the shared state is made ready. [*Note 4*: `f` might not be called at all, so its completion might never happen. — *end note*]

If the implementation chooses the `launch::async` policy,

- a call to a waiting function on an asynchronous return object that shares the shared state created by this `async` call shall block until the associated thread has completed, as if joined, or else time out ([[thread.thread.member]](thread.thread.member.md));
- the associated thread completion synchronizes with ([[intro.multithread]](../../basic/intro.multithread.md)) the return from the first function that successfully detects the ready status of the shared state or with the return from the last function that releases the shared state, whichever happens first.

**7** *Throws:* `system_error` if `policy == launch::async` and the implementation is unable to start a new thread, or `std::bad_alloc` if memory for the internal data structures could not be allocated.

**8** *Error conditions:*

- `resource_unavailable_try_again` — if `policy == launch::async` and the system is unable to start a new thread.

**9** [*Example 1*:

```cpp
int work1(int value);
int work2(int value);
int work(int value) {
  auto handle = std::async([=]{ return work2(value); });
  int tmp = work1(value);
  return tmp + handle.get();    // #1
}
```

[*Note 5*: Line #1 might not result in concurrency because the `async` call uses the default policy, which may use `launch::deferred`, in which case the lambda might not be invoked until the `get()` call; in that case, `work1` and `work2` are called on the same thread and there is no concurrency. — *end note*] — *end example*]
