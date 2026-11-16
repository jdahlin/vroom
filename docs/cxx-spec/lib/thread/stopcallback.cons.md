# 32.3.5.1 Constructors and destructor [stopcallback.cons]

[Contents](../../README.md) · up: [32.3.5 Class template `stop_callback` \[stopcallback\]](stopcallback.md) · prev: [32.3.5 Class template `stop_callback` \[stopcallback\]](stopcallback.md) · next: [32.4 Threads \[thread.threads\]](thread.threads.md)

```cpp
template<class C>
explicit stop_callback(const stop_token& st, C&& cb)
  noexcept(is_nothrow_constructible_v<Callback, C>);
template<class C>
explicit stop_callback(stop_token&& st, C&& cb)
  noexcept(is_nothrow_constructible_v<Callback, C>);
```

**1** *Constraints:* `Callback` and `C` satisfy `constructible_from<Callback, C>`.

**2** *Preconditions:* `Callback` and `C` model `constructible_from<Callback, C>`.

**3** *Effects:* Initializes `callback` with `std::forward<C>(cb)`. If `st.stop_requested()` is `true`, then `std::forward<Callback>(callback)()` is evaluated in the current thread before the constructor returns. Otherwise, if `st` has ownership of a stop state, acquires shared ownership of that stop state and registers the callback with that stop state such that `std::forward<Callback>(callback)()` is evaluated by the first call to `request_stop()` on an associated `stop_source`.

**4** *Remarks:* If evaluating `std::forward<Callback>(callback)()` exits via an exception, then `terminate` is called ([[except.terminate]](../../except/except.terminate.md)).

**5** *Throws:* Any exception thrown by the initialization of `callback`.

```cpp
~stop_callback();
```

**6** *Effects:* Unregisters the callback from the owned stop state, if any. The destructor does not block waiting for the execution of another callback registered by an associated `stop_callback`. If `callback` is concurrently executing on another thread, then the return from the invocation of `callback` strongly happens before ([[intro.races]](../../basic/intro.races.md)) `callback` is destroyed. If `callback` is executing on the current thread, then the destructor does not block ([[defns.block]](../../intro/intro.defs.md#defns.block)) waiting for the return from the invocation of `callback`. Releases ownership of the stop state, if any.
