# 32.5.4.3.2 Locking [thread.lock.unique.locking]

[Contents](../../README.md) · up: [32.5.4.3 Class template `unique_lock` \[thread.lock.unique\]](thread.lock.unique.md) · prev: [32.5.4.3.1 Constructors, destructor, and assignment \[thread.lock.unique.cons\]](thread.lock.unique.cons.md) · next: [32.5.4.3.3 Modifiers \[thread.lock.unique.mod\]](thread.lock.unique.mod.md)

```cpp
void lock();
```

**1** *Effects:* As if by `pm->lock()`.

**2** *Postconditions:* `owns == true`.

**3** *Throws:* Any exception thrown by `pm->lock()`. `system_error` when an exception is required ([[thread.req.exception]](thread.req.exception.md)).

**4** *Error conditions:*

- `operation_not_permitted` — if `pm` is `nullptr`.
- `resource_deadlock_would_occur` — if on entry `owns` is `true`.

```cpp
bool try_lock();
```

**5** *Preconditions:* The supplied `Mutex` meets the *Cpp17Lockable* requirements ([[thread.req.lockable.req]](thread.req.lockable.req.md)).

**6** *Effects:* As if by `pm->try_lock()`.

**7** *Returns:* The value returned by the call to `try_lock()`.

**8** *Postconditions:* `owns == res`, where `res` is the value returned by the call to `try_lock()`.

**9** *Throws:* Any exception thrown by `pm->try_lock()`. `system_error` when an exception is required ([[thread.req.exception]](thread.req.exception.md)).

**10** *Error conditions:*

- `operation_not_permitted` — if `pm` is `nullptr`.
- `resource_deadlock_would_occur` — if on entry `owns` is `true`.

```cpp
template<class Clock, class Duration>
  bool try_lock_until(const chrono::time_point<Clock, Duration>& abs_time);
```

**11** *Preconditions:* The supplied `Mutex` type meets the *Cpp17TimedLockable* requirements ([[thread.req.lockable.timed]](thread.req.lockable.timed.md)).

**12** *Effects:* As if by `pm->try_lock_until(abs_time)`.

**13** *Returns:* The value returned by the call to `try_lock_until(abs_time)`.

**14** *Postconditions:* `owns == res`, where `res` is the value returned by the call to `try_lock_until(abs_time)`.

**15** *Throws:* Any exception thrown by `pm->try_lock_until()`. `system_error` when an exception is required ([[thread.req.exception]](thread.req.exception.md)).

**16** *Error conditions:*

- `operation_not_permitted` — if `pm` is `nullptr`.
- `resource_deadlock_would_occur` — if on entry `owns` is `true`.

```cpp
template<class Rep, class Period>
  bool try_lock_for(const chrono::duration<Rep, Period>& rel_time);
```

**17** *Preconditions:* The supplied `Mutex` type meets the *Cpp17TimedLockable* requirements ([[thread.req.lockable.timed]](thread.req.lockable.timed.md)).

**18** *Effects:* As if by `pm->try_lock_for(rel_time)`.

**19** *Returns:* The value returned by the call to `try_lock_for(rel_time)`.

**20** *Postconditions:* `owns == res`, where `res` is the value returned by the call to `try_lock_for(rel_time)`.

**21** *Throws:* Any exception thrown by `pm->try_lock_for()`. `system_error` when an exception is required ([[thread.req.exception]](thread.req.exception.md)).

**22** *Error conditions:*

- `operation_not_permitted` — if `pm` is `nullptr`.
- `resource_deadlock_would_occur` — if on entry `owns` is `true`.

```cpp
void unlock();
```

**23** *Effects:* As if by `pm->unlock()`.

**24** *Postconditions:* `owns == false`.

**25** *Throws:* `system_error` when an exception is required ([[thread.req.exception]](thread.req.exception.md)).

**26** *Error conditions:*

- `operation_not_permitted` — if on entry `owns` is `false`.
