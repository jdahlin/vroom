# 32.9.8 Class template `shared_future` [futures.shared.future]

[Contents](../../README.md) · up: [32.9 Futures \[futures\]](futures.md) · prev: [32.9.7 Class template `future` \[futures.unique.future\]](futures.unique.future.md) · next: [32.9.9 Function template `async` \[futures.async\]](futures.async.md)

**1** The class template `shared_future` defines a type for asynchronous return objects which may share their shared state with other asynchronous return objects. A default-constructed `shared_future` object has no shared state. A `shared_future` object with shared state can be created by conversion from a `future` object and shares its shared state with the original asynchronous provider ([[futures.state]](futures.state.md)) of the shared state. The result (value or exception) of a `shared_future` object can be set by calling a respective function on an object that shares the same shared state.

**2** [*Note 1*: Member functions of `shared_future` do not synchronize with themselves, but they synchronize with the shared state. — *end note*]

**3** The effect of calling any member function other than the destructor, the move-assignment operator, the copy-assignment operator, or `valid()` on a `shared_future` object for which `valid() == false` is undefined. [*Note 2*: It is valid to copy or move from a `shared_future` object for which `valid()` is `false`. — *end note*] [*Note 3*: Implementations should detect this case and throw an object of type `future_error` with an error condition of `future_errc::no_state`. — *end note*]

```cpp
namespace std {
  template<class R>
  class shared_future {
  public:
    shared_future() noexcept;
    shared_future(const shared_future& rhs) noexcept;
    shared_future(future<R>&&) noexcept;
    shared_future(shared_future&& rhs) noexcept;
    ~shared_future();
    shared_future& operator=(const shared_future& rhs) noexcept;
    shared_future& operator=(shared_future&& rhs) noexcept;

    // retrieving the value
    see below get() const;

    // functions to check state
    bool valid() const noexcept;

    void wait() const;
    template<class Rep, class Period>
      future_status wait_for(const chrono::duration<Rep, Period>& rel_time) const;
    template<class Clock, class Duration>
      future_status wait_until(const chrono::time_point<Clock, Duration>& abs_time) const;
  };
}
```

**4** The implementation provides the template `shared_future` and two specializations, `shared_future<R&>` and `shared_future<void>`. These differ only in the return type and return value of the member function `get`, as set out in its description, below.

```cpp
shared_future() noexcept;
```

**5** *Effects:* The object does not refer to a shared state.

**6** *Postconditions:* `valid() == false`.

```cpp
shared_future(const shared_future& rhs) noexcept;
```

**7** *Effects:* The object refers to the same shared state as `rhs` (if any).

**8** *Postconditions:* `valid()` returns the same value as `rhs.valid()`.

```cpp
shared_future(future<R>&& rhs) noexcept;
shared_future(shared_future&& rhs) noexcept;
```

**9** *Effects:* Move constructs a `shared_future` object that refers to the shared state that was originally referred to by `rhs` (if any).

**10** *Postconditions:*

- `valid()` returns the same value as `rhs.valid()` returned prior to the constructor invocation.
- `rhs.valid() == false`.

```cpp
~shared_future();
```

**11** *Effects:*

- Releases any shared state ([[futures.state]](futures.state.md));
- destroys `*this`.

```cpp
shared_future& operator=(shared_future&& rhs) noexcept;
```

**12** *Effects:*

- Releases any shared state ([[futures.state]](futures.state.md));
- move assigns the contents of `rhs` to `*this`.

**13** *Postconditions:*

- `valid()` returns the same value as `rhs.valid()` returned prior to the assignment.
- `rhs.valid() == false`.

```cpp
shared_future& operator=(const shared_future& rhs) noexcept;
```

**14** *Effects:*

- Releases any shared state ([[futures.state]](futures.state.md));
- assigns the contents of `rhs` to `*this`. [*Note 4*: As a result, `*this` refers to the same shared state as `rhs` (if any). — *end note*]

**15** *Postconditions:* `valid() == rhs.valid()`.

```cpp
const R& shared_future::get() const;
R& shared_future<R&>::get() const;
void shared_future<void>::get() const;
```

**16** [*Note 5*: As described above, the template and its two required specializations differ only in the return type and return value of the member function `get`. — *end note*]

**17** [*Note 6*: Access to a value object stored in the shared state is unsynchronized, so programmers should apply only those operations on `R` that do not introduce a data race ([[intro.multithread]](../../basic/intro.multithread.md)). — *end note*]

**18** *Effects:* `wait()`s until the shared state is ready, then retrieves the value stored in the shared state.

**19** *Returns:*

- `shared_future::get()` returns a const reference to the value stored in the object’s shared state. [*Note 7*: Access through that reference after the shared state has been destroyed produces undefined behavior; this can be avoided by not storing the reference in any storage with a greater lifetime than the `shared_future` object that returned the reference. — *end note*]
- `shared_future<R&>::get()` returns the reference stored as value in the object’s shared state.
- `shared_future<void>::get()` returns nothing.

**20** *Throws:* The stored exception, if an exception was stored in the shared state.

```cpp
bool valid() const noexcept;
```

**21** *Returns:* `true` only if `*this` refers to a shared state.

```cpp
void wait() const;
```

**22** *Effects:* Blocks until the shared state is ready.

```cpp
template<class Rep, class Period>
  future_status wait_for(const chrono::duration<Rep, Period>& rel_time) const;
```

**23** *Effects:* None if the shared state contains a deferred function ([[futures.async]](futures.async.md)), otherwise blocks until the shared state is ready or until the relative timeout ([[thread.req.timing]](thread.req.timing.md)) specified by `rel_time` has expired.

**24** *Returns:*

- `future_status::deferred` if the shared state contains a deferred function.
- `future_status::ready` if the shared state is ready.
- `future_status::timeout` if the function is returning because the relative timeout ([[thread.req.timing]](thread.req.timing.md)) specified by `rel_time` has expired.

**25** *Throws:* timeout-related exceptions ([[thread.req.timing]](thread.req.timing.md)).

```cpp
template<class Clock, class Duration>
  future_status wait_until(const chrono::time_point<Clock, Duration>& abs_time) const;
```

**26** *Effects:* None if the shared state contains a deferred function ([[futures.async]](futures.async.md)), otherwise blocks until the shared state is ready or until the absolute timeout ([[thread.req.timing]](thread.req.timing.md)) specified by `abs_time` has expired.

**27** *Returns:*

- `future_status::deferred` if the shared state contains a deferred function.
- `future_status::ready` if the shared state is ready.
- `future_status::timeout` if the function is returning because the absolute timeout ([[thread.req.timing]](thread.req.timing.md)) specified by `abs_time` has expired.

**28** *Throws:* timeout-related exceptions ([[thread.req.timing]](thread.req.timing.md)).
