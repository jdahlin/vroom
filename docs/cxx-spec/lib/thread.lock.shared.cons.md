# 32.5.4.4.1 Constructors, destructor, and assignment [thread.lock.shared.cons]

[Contents](../README.md) · up: [32.5.4.4 Class template `shared_lock` \[thread.lock.shared\]](thread.lock.shared.md) · prev: [32.5.4.4 Class template `shared_lock` \[thread.lock.shared\]](thread.lock.shared.md) · next: [32.5.4.4.2 Locking \[thread.lock.shared.locking\]](thread.lock.shared.locking.md)

```cpp
shared_lock() noexcept;
```

**1** *Postconditions:* `pm == nullptr` and `owns == false`.

```cpp
explicit shared_lock(mutex_type& m);
```

**2** *Preconditions:* The calling thread does not own the mutex for any ownership mode.

**3** *Effects:* Calls `m.lock_shared()`.

**4** *Postconditions:* `pm == addressof(m)` and `owns == true`.

```cpp
shared_lock(mutex_type& m, defer_lock_t) noexcept;
```

**5** *Postconditions:* `pm == addressof(m)` and `owns == false`.

```cpp
shared_lock(mutex_type& m, try_to_lock_t);
```

**6** *Preconditions:* The calling thread does not own the mutex for any ownership mode.

**7** *Effects:* Calls `m.try_lock_shared()`.

**8** *Postconditions:* `pm == addressof(m)` and `owns == res` where `res` is the value returned by the call to `m.try_lock_shared()`.

```cpp
shared_lock(mutex_type& m, adopt_lock_t);
```

**9** *Preconditions:* The calling thread has shared ownership of the mutex.

**10** *Postconditions:* `pm == addressof(m)` and `owns == true`.

```cpp
template<class Clock, class Duration>
  shared_lock(mutex_type& m,
              const chrono::time_point<Clock, Duration>& abs_time);
```

**11** *Preconditions:* The calling thread does not own the mutex for any ownership mode.

**12** *Effects:* Calls `m.try_lock_shared_until(abs_time)`.

**13** *Postconditions:* `pm == addressof(m)` and `owns == res` where `res` is the value returned by the call to `m.try_lock_shared_until(abs_time)`.

```cpp
template<class Rep, class Period>
  shared_lock(mutex_type& m,
              const chrono::duration<Rep, Period>& rel_time);
```

**14** *Preconditions:* The calling thread does not own the mutex for any ownership mode.

**15** *Effects:* Calls `m.try_lock_shared_for(rel_time)`.

**16** *Postconditions:* `pm == addressof(m)` and `owns == res` where `res` is the value returned by the call to `m.try_lock_shared_for(rel_time)`.

```cpp
~shared_lock();
```

**17** *Effects:* If `owns` calls `pm->unlock_shared()`.

```cpp
shared_lock(shared_lock&& sl) noexcept;
```

**18** *Postconditions:* `pm == sl_p.pm` and `owns == sl_p.owns` (where `sl_p` is the state of `sl` just prior to this construction), `sl.pm == nullptr` and `sl.owns == false`.

```cpp
shared_lock& operator=(shared_lock&& sl) noexcept;
```

**19** *Effects:* If `owns` calls `pm->unlock_shared()`.

**20** *Postconditions:* `pm == sl_p.pm` and `owns == sl_p.owns` (where `sl_p` is the state of `sl` just prior to this assignment), `sl.pm == nullptr` and `sl.owns == false`.
