# 32.5.4.3.1 Constructors, destructor, and assignment [thread.lock.unique.cons]

[Contents](../README.md) · up: [32.5.4.3 Class template `unique_lock` \[thread.lock.unique\]](thread.lock.unique.md) · prev: [32.5.4.3 Class template `unique_lock` \[thread.lock.unique\]](thread.lock.unique.md) · next: [32.5.4.3.2 Locking \[thread.lock.unique.locking\]](thread.lock.unique.locking.md)

```cpp
unique_lock() noexcept;
```

**1** *Postconditions:* `pm == 0` and `owns == false`.

```cpp
explicit unique_lock(mutex_type& m);
```

**2** *Preconditions:* If `mutex_type` is not a recursive mutex the calling thread does not own the mutex.

**3** *Effects:* Calls `m.lock()`.

**4** *Postconditions:* `pm == addressof(m)` and `owns == true`.

```cpp
unique_lock(mutex_type& m, defer_lock_t) noexcept;
```

**5** *Postconditions:* `pm == addressof(m)` and `owns == false`.

```cpp
unique_lock(mutex_type& m, try_to_lock_t);
```

**6** *Preconditions:* The supplied `Mutex` type meets the *Cpp17Lockable* requirements ([[thread.req.lockable.req]](thread.req.lockable.req.md)). If `mutex_type` is not a recursive mutex the calling thread does not own the mutex.

**7** *Effects:* Calls `m.try_lock()`.

**8** *Postconditions:* `pm == addressof(m)` and `owns == res`, where `res` is the value returned by the call to `m.try_lock()`.

```cpp
unique_lock(mutex_type& m, adopt_lock_t);
```

**9** *Preconditions:* The calling thread owns the mutex.

**10** *Postconditions:* `pm == addressof(m)` and `owns == true`.

**11** *Throws:* Nothing.

```cpp
template<class Clock, class Duration>
  unique_lock(mutex_type& m, const chrono::time_point<Clock, Duration>& abs_time);
```

**12** *Preconditions:* If `mutex_type` is not a recursive mutex the calling thread does not own the mutex. The supplied `Mutex` type meets the *Cpp17TimedLockable* requirements ([[thread.req.lockable.timed]](thread.req.lockable.timed.md)).

**13** *Effects:* Calls `m.try_lock_until(abs_time)`.

**14** *Postconditions:* `pm == addressof(m)` and `owns == res`, where `res` is the value returned by the call to `m.try_lock_until(abs_time)`.

```cpp
template<class Rep, class Period>
  unique_lock(mutex_type& m, const chrono::duration<Rep, Period>& rel_time);
```

**15** *Preconditions:* If `mutex_type` is not a recursive mutex the calling thread does not own the mutex. The supplied `Mutex` type meets the *Cpp17TimedLockable* requirements ([[thread.req.lockable.timed]](thread.req.lockable.timed.md)).

**16** *Effects:* Calls `m.try_lock_for(rel_time)`.

**17** *Postconditions:* `pm == addressof(m)` and `owns == res`, where `res` is the value returned by the call to `m.try_lock_for(rel_time)`.

```cpp
unique_lock(unique_lock&& u) noexcept;
```

**18** *Postconditions:* `pm == u_p.pm` and `owns == u_p.owns` (where `u_p` is the state of `u` just prior to this construction), `u.pm == 0` and `u.owns == false`.

```cpp
unique_lock& operator=(unique_lock&& u);
```

**19** *Effects:* If `owns` calls `pm->unlock()`.

**20** *Postconditions:* `pm == u_p.pm` and `owns == u_p.owns` (where `u_p` is the state of `u` just prior to this construction), `u.pm == 0` and `u.owns == false`.

**21** [*Note 1*: With a recursive mutex it is possible for both `*this` and `u` to own the same mutex before the assignment. In this case, `*this` will own the mutex after the assignment and `u` will not. — *end note*]

**22** *Throws:* Nothing.

```cpp
~unique_lock();
```

**23** *Effects:* If `owns` calls `pm->unlock()`.
