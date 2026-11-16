# 32.9.7 Class template `future` [futures.unique.future]

[Contents](../README.md) · up: [32.9 Futures \[futures\]](futures.md) · prev: [32.9.6 Class template `promise` \[futures.promise\]](futures.promise.md) · next: [32.9.8 Class template `shared_future` \[futures.shared.future\]](futures.shared.future.md)

**1** The class template `future` defines a type for asynchronous return objects which do not share their shared state with other asynchronous return objects. A default-constructed `future` object has no shared state. A `future` object with shared state can be created by functions on asynchronous providers ([[futures.state]](futures.state.md)) or by the move constructor and shares its shared state with the original asynchronous provider. The result (value or exception) of a `future` object can be set by calling a respective function on an object that shares the same shared state.

**2** [*Note 1*: Member functions of `future` do not synchronize with themselves or with member functions of `shared_future`. — *end note*]

**3** The effect of calling any member function other than the destructor, the move-assignment operator, `share`, or `valid` on a `future` object for which `valid() == false` is undefined. [*Note 2*: It is valid to move from a future object for which `valid() == false`. — *end note*] [*Note 3*: Implementations should detect this case and throw an object of type `future_error` with an error condition of `future_errc::no_state`. — *end note*]

```cpp
namespace std {
  template<class R>
  class future {
  public:
    future() noexcept;
    future(future&&) noexcept;
    future(const future&) = delete;
    ~future();
    future& operator=(const future&) = delete;
    future& operator=(future&&) noexcept;
    shared_future<R> share() noexcept;

    // retrieving the value
    see below get();

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

**4** The implementation provides the template `future` and two specializations, `future<R&>` and `future<void>`. These differ only in the return type and return value of the member function `get`, as set out in its description, below.

```cpp
future() noexcept;
```

**5** *Effects:* The object does not refer to a shared state.

**6** *Postconditions:* `valid() == false`.

```cpp
future(future&& rhs) noexcept;
```

**7** *Effects:* Move constructs a `future` object that refers to the shared state that was originally referred to by `rhs` (if any).

**8** *Postconditions:*

- `valid()` returns the same value as `rhs.valid()` prior to the constructor invocation.
- `rhs.valid() == false`.

```cpp
~future();
```

**9** *Effects:*

- Releases any shared state ([[futures.state]](futures.state.md));
- destroys `*this`.

```cpp
future& operator=(future&& rhs) noexcept;
```

**10** *Effects:*

- Releases any shared state ([[futures.state]](futures.state.md)).
- move assigns the contents of `rhs` to `*this`.

**11** *Postconditions:*

- `valid()` returns the same value as `rhs.valid()` prior to the assignment.
- `rhs.valid() == false`.

```cpp
shared_future<R> share() noexcept;
```

**12** *Returns:* `shared_future<R>(std::move(*this))`.

**13** *Postconditions:* `valid() == false`.

```cpp
R future::get();
R& future<R&>::get();
void future<void>::get();
```

**14** [*Note 4*: As described above, the template and its two required specializations differ only in the return type and return value of the member function `get`. — *end note*]

**15** *Effects:*

- `wait()`s until the shared state is ready, then retrieves the value stored in the shared state;
- releases any shared state ([[futures.state]](futures.state.md)).

**16** *Returns:*

- `future::get()` returns the value `v` stored in the object’s shared state as `std::move(v)`.
- `future<R&>::get()` returns the reference stored as value in the object’s shared state.
- `future<void>::get()` returns nothing.

**17** *Throws:* The stored exception, if an exception was stored in the shared state.

**18** *Postconditions:* `valid() == false`.

```cpp
bool valid() const noexcept;
```

**19** *Returns:* `true` only if `*this` refers to a shared state.

```cpp
void wait() const;
```

**20** *Effects:* Blocks until the shared state is ready.

```cpp
template<class Rep, class Period>
  future_status wait_for(const chrono::duration<Rep, Period>& rel_time) const;
```

**21** *Effects:* None if the shared state contains a deferred function ([[futures.async]](futures.async.md)), otherwise blocks until the shared state is ready or until the relative timeout ([[thread.req.timing]](thread.req.timing.md)) specified by `rel_time` has expired.

**22** *Returns:*

- `future_status::deferred` if the shared state contains a deferred function.
- `future_status::ready` if the shared state is ready.
- `future_status::timeout` if the function is returning because the relative timeout ([[thread.req.timing]](thread.req.timing.md)) specified by `rel_time` has expired.

**23** *Throws:* timeout-related exceptions ([[thread.req.timing]](thread.req.timing.md)).

```cpp
template<class Clock, class Duration>
  future_status wait_until(const chrono::time_point<Clock, Duration>& abs_time) const;
```

**24** *Effects:* None if the shared state contains a deferred function ([[futures.async]](futures.async.md)), otherwise blocks until the shared state is ready or until the absolute timeout ([[thread.req.timing]](thread.req.timing.md)) specified by `abs_time` has expired.

**25** *Returns:*

- `future_status::deferred` if the shared state contains a deferred function.
- `future_status::ready` if the shared state is ready.
- `future_status::timeout` if the function is returning because the absolute timeout ([[thread.req.timing]](thread.req.timing.md)) specified by `abs_time` has expired.

**26** *Throws:* timeout-related exceptions ([[thread.req.timing]](thread.req.timing.md)).
