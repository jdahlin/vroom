# 32.9.6 Class template `promise` [futures.promise]

[Contents](../README.md) · up: [32.9 Futures \[futures\]](futures.md) · prev: [32.9.5 Shared state \[futures.state\]](futures.state.md) · next: [32.9.7 Class template `future` \[futures.unique.future\]](futures.unique.future.md)

```cpp
namespace std {
  template<class R>
  class promise {
  public:
    promise();
    template<class Allocator>
      promise(allocator_arg_t, const Allocator& a);
    promise(promise&& rhs) noexcept;
    promise(const promise&) = delete;
    ~promise();

    // assignment
    promise& operator=(promise&& rhs) noexcept;
    promise& operator=(const promise&) = delete;
    void swap(promise& other) noexcept;

    // retrieving the result
    future<R> get_future();

    // setting the result
    void set_value(see below);
    void set_exception(exception_ptr p);

    // setting the result with deferred notification
    void set_value_at_thread_exit(see below);
    void set_exception_at_thread_exit(exception_ptr p);
  };

  template<class R>
    void swap(promise<R>& x, promise<R>& y) noexcept;

  template<class R, class Alloc>
    struct uses_allocator<promise<R>, Alloc>;
}
```

**1** The implementation provides the template `promise` and two specializations, `promise<R&>` and `promise<void>`. These differ only in the argument type of the member functions `set_value` and `set_value_at_thread_exit`, as set out in their descriptions, below.

**2** The `set_value`, `set_exception`, `set_value_at_thread_exit`, and `set_exception_at_thread_exit` member functions behave as though they acquire a single mutex associated with the promise object while updating the promise object.

```cpp
template<class R, class Alloc>
  struct uses_allocator<promise<R>, Alloc>
    : true_type { };
```

**3** *Preconditions:* `Alloc` meets the *Cpp17Allocator* requirements ([Table 36](allocator.requirements.md#tab:cpp17.allocator)).

```cpp
promise();
template<class Allocator>
  promise(allocator_arg_t, const Allocator& a);
```

**4** *Effects:* Creates a shared state. The second constructor uses the allocator `a` to allocate memory for the shared state.

```cpp
promise(promise&& rhs) noexcept;
```

**5** *Effects:* Transfers ownership of the shared state of `rhs` (if any) to the newly-constructed object.

**6** *Postconditions:* `rhs` has no shared state.

```cpp
~promise();
```

**7** *Effects:* Abandons any shared state ([[futures.state]](futures.state.md)).

```cpp
promise& operator=(promise&& rhs) noexcept;
```

**8** *Effects:* Abandons any shared state ([[futures.state]](futures.state.md)) and then as if `promise(std::move(rhs)).swap(*this)`.

**9** *Returns:* `*this`.

```cpp
void swap(promise& other) noexcept;
```

**10** *Effects:* Exchanges the shared state of `*this` and `other`.

**11** *Postconditions:* `*this` has the shared state (if any) that `other` had prior to the call to `swap`. `other` has the shared state (if any) that `*this` had prior to the call to `swap`.

```cpp
future<R> get_future();
```

**12** *Returns:* A `future<R>` object with the same shared state as `*this`.

**13** *Synchronization:* Calls to this function do not introduce data races ([[intro.multithread]](../intro.multithread.md)) with calls to `set_value`, `set_exception`, `set_value_at_thread_exit`, or `set_exception_at_thread_exit`. [*Note 1*: Such calls need not synchronize with each other. — *end note*]

**14** *Throws:* `future_error` if `*this` has no shared state or if `get_future` has already been called on a `promise` with the same shared state as `*this`.

**15** *Error conditions:*

- `future_already_retrieved` if `get_future` has already been called on a `promise` with the same shared state as `*this`.
- `no_state` if `*this` has no shared state.

```cpp
void promise::set_value(const R& r);
void promise::set_value(R&& r);
void promise<R&>::set_value(R& r);
void promise<void>::set_value();
```

**16** *Effects:* Atomically stores the value `r` in the shared state and makes that state ready ([[futures.state]](futures.state.md)).

**17** *Throws:*

- `future_error` if its shared state already has a stored value or exception, or
- for the first version, any exception thrown by the constructor selected to copy an object of `R`, or
- for the second version, any exception thrown by the constructor selected to move an object of `R`.

**18** *Error conditions:*

- `promise_already_satisfied` if its shared state already has a stored value or exception.
- `no_state` if `*this` has no shared state.

```cpp
void set_exception(exception_ptr p);
```

**19** *Preconditions:* `p` is not null.

**20** *Effects:* Atomically stores the exception pointer `p` in the shared state and makes that state ready ([[futures.state]](futures.state.md)).

**21** *Throws:* `future_error` if its shared state already has a stored value or exception.

**22** *Error conditions:*

- `promise_already_satisfied` if its shared state already has a stored value or exception.
- `no_state` if `*this` has no shared state.

```cpp
void promise::set_value_at_thread_exit(const R& r);
void promise::set_value_at_thread_exit(R&& r);
void promise<R&>::set_value_at_thread_exit(R& r);
void promise<void>::set_value_at_thread_exit();
```

**23** *Effects:* Stores the value `r` in the shared state without making that state ready immediately. Schedules that state to be made ready when the current thread exits, after all objects of thread storage duration associated with the current thread have been destroyed.

**24** *Throws:*

- `future_error` if its shared state already has a stored value or exception, or
- for the first version, any exception thrown by the constructor selected to copy an object of `R`, or
- for the second version, any exception thrown by the constructor selected to move an object of `R`.

**25** *Error conditions:*

- `promise_already_satisfied` if its shared state already has a stored value or exception.
- `no_state` if `*this` has no shared state.

```cpp
void set_exception_at_thread_exit(exception_ptr p);
```

**26** *Preconditions:* `p` is not null.

**27** *Effects:* Stores the exception pointer `p` in the shared state without making that state ready immediately. Schedules that state to be made ready when the current thread exits, after all objects of thread storage duration associated with the current thread have been destroyed.

**28** *Throws:* `future_error` if an error condition occurs.

**29** *Error conditions:*

- `promise_already_satisfied` if its shared state already has a stored value or exception.
- `no_state` if `*this` has no shared state.

```cpp
template<class R>
  void swap(promise<R>& x, promise<R>& y) noexcept;
```

**30** *Effects:* As if by `x.swap(y)`.
