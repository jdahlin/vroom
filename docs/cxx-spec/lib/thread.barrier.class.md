# 32.8.2.2 Class template `barrier` [thread.barrier.class]

[Contents](../README.md) · up: [32.8.2 Barriers \[thread.barrier\]](thread.barrier.md) · prev: [32.8.2.1 Header `<barrier>` synopsis \[barrier.syn\]](barrier.syn.md) · next: [32.9 Futures \[futures\]](futures.md)

```cpp
namespace std {
  template<class CompletionFunction = see below>
  class barrier {
  public:
    using arrival_token = see below;

    static constexpr ptrdiff_t max() noexcept;

    constexpr explicit barrier(ptrdiff_t expected,
                               CompletionFunction f = CompletionFunction());
    ~barrier();

    barrier(const barrier&) = delete;
    barrier& operator=(const barrier&) = delete;

    [[nodiscard]] arrival_token arrive(ptrdiff_t update = 1);
    void wait(arrival_token&& arrival) const;

    void arrive_and_wait();
    void arrive_and_drop();

  private:
    CompletionFunction completion;      // exposition only
  };
}
```

**1** Each *barrier phase* consists of the following steps:

- The expected count is decremented by each call to `arrive` or `arrive_and_drop`.
- When the expected count reaches zero, the phase completion step is run. For the specialization with the default value of the `CompletionFunction` template parameter, the completion step is run as part of the call to `arrive` or `arrive_and_drop` that caused the expected count to reach zero. For other specializations, the completion step is run on one of the threads that arrived at the barrier during the phase.
- When the completion step finishes, the expected count is reset to what was specified by the `expected` argument to the constructor, possibly adjusted by calls to `arrive_and_drop`, and the next phase starts.

**2** Each phase defines a *phase synchronization point*. Threads that arrive at the barrier during the phase can block on the phase synchronization point by calling `wait`, and will remain blocked until the phase completion step is run.

**3** The *phase completion step* that is executed at the end of each phase has the following effects:

- Invokes the completion function, equivalent to `completion()`.
- Unblocks all threads that are blocked on the phase synchronization point.

The end of the completion step strongly happens before the returns from all calls that were unblocked by the completion step. For specializations that do not have the default value of the `CompletionFunction` template parameter, the behavior is undefined if any of the barrier object’s member functions other than `wait` are called while the completion step is in progress.

**4** Concurrent invocations of the member functions of `barrier`, other than its destructor, do not introduce data races. The member functions `arrive` and `arrive_and_drop` execute atomically.

**5** `CompletionFunction` shall meet the *Cpp17MoveConstructible* ([Table 28](utility.arg.requirements.md#tab:cpp17.moveconstructible)) and *Cpp17Destructible* ([Table 32](utility.arg.requirements.md#tab:cpp17.destructible)) requirements. `is_nothrow_invocable_v<CompletionFunction&>` shall be `true`.

**6** The default value of the `CompletionFunction` template parameter is an unspecified type, such that, in addition to satisfying the requirements of `CompletionFunction`, it meets the *Cpp17DefaultConstructible* requirements ([Table 27](utility.arg.requirements.md#tab:cpp17.defaultconstructible)) and `completion()` has no effects.

**7** `barrier::arrival_token` is an unspecified type, such that it meets the *Cpp17MoveConstructible* ([Table 28](utility.arg.requirements.md#tab:cpp17.moveconstructible)), *Cpp17MoveAssignable* ([Table 30](utility.arg.requirements.md#tab:cpp17.moveassignable)), and *Cpp17Destructible* ([Table 32](utility.arg.requirements.md#tab:cpp17.destructible)) requirements.

```cpp
static constexpr ptrdiff_t max() noexcept;
```

**8** *Returns:* The maximum expected count that the implementation supports.

```cpp
constexpr explicit barrier(ptrdiff_t expected,
                           CompletionFunction f = CompletionFunction());
```

**9** *Preconditions:* `expected >= 0` is `true` and `expected <= max()` is `true`.

**10** *Effects:* Sets both the initial expected count for each barrier phase and the current expected count for the first phase to `expected`. Initializes `completion` with `std::move(f)`. Starts the first phase. [*Note 1*: If `expected` is 0 this object can only be destroyed. — *end note*]

**11** *Throws:* Any exception thrown by `CompletionFunction`’s move constructor.

```cpp
[[nodiscard]] arrival_token arrive(ptrdiff_t update = 1);
```

**12** *Preconditions:* `update > 0` is `true`, and `update` is less than or equal to the expected count for the current barrier phase.

**13** *Effects:* Constructs an object of type `arrival_token` that is associated with the phase synchronization point for the current phase. Then, decrements the expected count by `update`.

**14** *Synchronization:* The call to `arrive` strongly happens before the start of the phase completion step for the current phase.

**15** *Returns:* The constructed `arrival_token` object.

**16** *Throws:* `system_error` when an exception is required ([[thread.req.exception]](thread.req.exception.md)).

**17** *Error conditions:* Any of the error conditions allowed for mutex types ([[thread.mutex.requirements.mutex]](thread.mutex.requirements.mutex.md)).

**18** [*Note 2*: This call can cause the completion step for the current phase to start. — *end note*]

```cpp
void wait(arrival_token&& arrival) const;
```

**19** *Preconditions:* `arrival` is associated with the phase synchronization point for the current phase or the immediately preceding phase of the same barrier object.

**20** *Effects:* Blocks at the synchronization point associated with `std::move(arrival)` until the phase completion step of the synchronization point’s phase is run. [*Note 3*: If `arrival` is associated with the synchronization point for a previous phase, the call returns immediately. — *end note*]

**21** *Throws:* `system_error` when an exception is required ([[thread.req.exception]](thread.req.exception.md)).

**22** *Error conditions:* Any of the error conditions allowed for mutex types ([[thread.mutex.requirements.mutex]](thread.mutex.requirements.mutex.md)).

```cpp
void arrive_and_wait();
```

**23** *Effects:* Equivalent to: `wait(arrive())`.

```cpp
void arrive_and_drop();
```

**24** *Preconditions:* The expected count for the current barrier phase is greater than zero.

**25** *Effects:* Decrements the initial expected count for all subsequent phases by one. Then decrements the expected count for the current phase by one.

**26** *Synchronization:* The call to `arrive_and_drop` strongly happens before the start of the phase completion step for the current phase.

**27** *Throws:* `system_error` when an exception is required ([[thread.req.exception]](thread.req.exception.md)).

**28** *Error conditions:* Any of the error conditions allowed for mutex types ([[thread.mutex.requirements.mutex]](thread.mutex.requirements.mutex.md)).

**29** [*Note 4*: This call can cause the completion step for the current phase to start. — *end note*]
