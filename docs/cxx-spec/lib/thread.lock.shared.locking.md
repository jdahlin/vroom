# 32.5.4.4.2 Locking [thread.lock.shared.locking]

[Contents](../README.md) · up: [32.5.4.4 Class template `shared_lock` \[thread.lock.shared\]](thread.lock.shared.md) · prev: [32.5.4.4.1 Constructors, destructor, and assignment \[thread.lock.shared.cons\]](thread.lock.shared.cons.md) · next: [32.5.4.4.3 Modifiers \[thread.lock.shared.mod\]](thread.lock.shared.mod.md)

```cpp
void lock();
```

**1** *Effects:* As if by `pm->lock_shared()`.

**2** *Postconditions:* `owns == true`.

**3** *Throws:* Any exception thrown by `pm->lock_shared()`. `system_error` when an exception is required ([[thread.req.exception]](thread.req.exception.md)).

**4** *Error conditions:*

- `operation_not_permitted` — if `pm` is `nullptr`.
- `resource_deadlock_would_occur` — if on entry `owns` is `true`.

```cpp
bool try_lock();
```

**5** *Effects:* As if by `pm->try_lock_shared()`.

**6** *Returns:* The value returned by the call to `pm->try_lock_shared()`.

**7** *Postconditions:* `owns == res`, where `res` is the value returned by the call to `pm->try_lock_shared()`.

**8** *Throws:* Any exception thrown by `pm->try_lock_shared()`. `system_error` when an exception is required ([[thread.req.exception]](thread.req.exception.md)).

**9** *Error conditions:*

- `operation_not_permitted` — if `pm` is `nullptr`.
- `resource_deadlock_would_occur` — if on entry `owns` is `true`.

```cpp
template<class Clock, class Duration>
  bool try_lock_until(const chrono::time_point<Clock, Duration>& abs_time);
```

**10** *Effects:* As if by `pm->try_lock_shared_until(abs_time)`.

**11** *Returns:* The value returned by the call to `pm->try_lock_shared_until(abs_time)`.

**12** *Postconditions:* `owns == res`, where `res` is the value returned by the call to `pm->try_lock_shared_until(abs_time)`.

**13** *Throws:* Any exception thrown by `pm->try_lock_shared_until(abs_time)`. `system_error` when an exception is required ([[thread.req.exception]](thread.req.exception.md)).

**14** *Error conditions:*

- `operation_not_permitted` — if `pm` is `nullptr`.
- `resource_deadlock_would_occur` — if on entry `owns` is `true`.

```cpp
template<class Rep, class Period>
  bool try_lock_for(const chrono::duration<Rep, Period>& rel_time);
```

**15** *Effects:* As if by `pm->try_lock_shared_for(rel_time)`.

**16** *Returns:* The value returned by the call to `pm->try_lock_shared_for(rel_time)`.

**17** *Postconditions:* `owns == res`, where `res` is the value returned by the call to `pm->try_lock_shared_for(rel_time)`.

**18** *Throws:* Any exception thrown by `pm->try_lock_shared_for(rel_time)`. `system_error` when an exception is required ([[thread.req.exception]](thread.req.exception.md)).

**19** *Error conditions:*

- `operation_not_permitted` — if `pm` is `nullptr`.
- `resource_deadlock_would_occur` — if on entry `owns` is `true`.

```cpp
void unlock();
```

**20** *Effects:* As if by `pm->unlock_shared()`.

**21** *Postconditions:* `owns == false`.

**22** *Throws:* `system_error` when an exception is required ([[thread.req.exception]](thread.req.exception.md)).

**23** *Error conditions:*

- `operation_not_permitted` — if on entry `owns` is `false`.
