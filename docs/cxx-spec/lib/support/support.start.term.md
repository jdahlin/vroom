# 17.5 Startup and termination [support.start.term]

[Contents](../../README.md) · up: [17 Language support library \[support\]](support.md) · prev: [17.4.1 Header `<cstdint>` synopsis \[cstdint.syn\]](cstdint.syn.md) · next: [17.6 Dynamic memory management \[support.dynamic\]](support.dynamic.md)

**1** [*Note 1*: The header `<cstdlib>` ([[cstdlib.syn]](cstdlib.syn.md)) declares the functions described in this subclause. — *end note*]

```cpp
[[noreturn]] void _Exit(int status) noexcept;
```

**2** *Effects:* This function has the semantics specified in the C standard library.

**3** *Remarks:* The program is terminated without executing destructors for objects of automatic, thread, or static storage duration and without calling functions passed to `atexit()` ([[basic.start.term]](../../basic/basic.start.term.md)). The function `_Exit` is signal-safe ([[support.signal]](support.signal.md)).

```cpp
[[noreturn]] void abort() noexcept;
```

**4** *Effects:* This function has the semantics specified in the C standard library.

**5** *Remarks:* The program is terminated without executing destructors for objects of automatic, thread, or static storage duration and without calling functions passed to `atexit()` ([[basic.start.term]](../../basic/basic.start.term.md)). The function `abort` is signal-safe ([[support.signal]](support.signal.md)).

```cpp
int atexit(c-atexit-handler* f) noexcept;
int atexit(atexit-handler* f) noexcept;
```

**6** *Effects:* The `atexit()` functions register the function pointed to by `f` to be called without arguments at normal program termination. It is unspecified whether a call to `atexit()` that does not happen before ([[intro.multithread]](../../basic/intro.multithread.md)) a call to `exit()` will succeed. [*Note 2*: The `atexit()` functions do not introduce a data race ([[res.on.data.races]](../library/res.on.data.races.md)). — *end note*]

**7** *Implementation limits:* The implementation shall support the registration of at least 32 functions.

**8** *Returns:* The `atexit()` function returns zero if the registration succeeds, nonzero if it fails.

```cpp
[[noreturn]] void exit(int status);
```

**9** *Effects:*

- First, objects with thread storage duration and associated with the current thread are destroyed. Next, objects with static storage duration are destroyed and functions registered by calling `atexit` are called.[^1] See [[basic.start.term]](../../basic/basic.start.term.md) for the order of destructions and calls. (Objects with automatic storage duration are not destroyed as a result of calling `exit()`.)[^2]

  If control leaves a registered function called by `exit` because the function does not provide a handler for a thrown exception, the function `std::terminate` shall be called ([[except.terminate]](../../except/except.terminate.md)).

- Next, all open C streams (as mediated by the function signatures declared in `<cstdio>` ([[cstdio.syn]](../io/cstdio.syn.md))) with unwritten buffered data are flushed, all open C streams are closed, and all files created by calling `tmpfile()` are removed.

- Finally, control is returned to the host environment. If `status` is zero or `EXIT_SUCCESS`, an implementation-defined form of the status *successful termination* is returned. If `status` is `EXIT_FAILURE`, an implementation-defined form of the status *unsuccessful termination* is returned. Otherwise the status returned is implementation-defined.[^3]

```cpp
int at_quick_exit(c-atexit-handler* f) noexcept;
int at_quick_exit(atexit-handler* f) noexcept;
```

**10** *Effects:* The `at_quick_exit()` functions register the function pointed to by `f` to be called without arguments when `quick_exit` is called. It is unspecified whether a call to `at_quick_exit()` that does not happen before ([[intro.multithread]](../../basic/intro.multithread.md)) all calls to `quick_exit` will succeed. [*Note 3*: The `at_quick_exit()` functions do not introduce a data race ([[res.on.data.races]](../library/res.on.data.races.md)). — *end note*] [*Note 4*: The order of registration may be indeterminate if `at_quick_exit` was called from more than one thread. — *end note*] [*Note 5*: The `at_quick_exit` registrations are distinct from the `atexit` registrations, and applications may need to call both registration functions with the same argument. — *end note*]

**11** *Implementation limits:* The implementation shall support the registration of at least 32 functions.

**12** *Returns:* Zero if the registration succeeds, nonzero if it fails.

```cpp
[[noreturn]] void quick_exit(int status) noexcept;
```

**13** *Effects:* Functions registered by calls to `at_quick_exit` are called in the reverse order of their registration, except that a function shall be called after any previously registered functions that had already been called at the time it was registered. Objects shall not be destroyed as a result of calling `quick_exit`. If control leaves a registered function called by `quick_exit` because the function does not provide a handler for a thrown exception, the function `std::terminate` shall be called. [*Note 6*: A function registered via `at_quick_exit` is invoked by the thread that calls `quick_exit`, which can be a different thread than the one that registered it, so registered functions should not rely on the identity of objects with thread storage duration. — *end note*] After calling registered functions, `quick_exit` shall call `_Exit(status)`.

**14** *Remarks:* The function `quick_exit` is signal-safe ([[support.signal]](support.signal.md)) when the functions registered with `at_quick_exit` are.

*See also:* ISO C 7.22.4

[^1]: A function is called for every time it is registered.
[^2]: Objects with automatic storage duration are all destroyed in a program whose `main` function ([[basic.start.main]](../../basic/basic.start.main.md)) contains no objects with automatic storage duration and executes the call to `exit()`. Control can be transferred directly to such a `main` function by throwing an exception that is caught in `main`.
[^3]: The macros `EXIT_FAILURE` and `EXIT_SUCCESS` are defined in `<cstdlib>` ([[cstdlib.syn]](cstdlib.syn.md)).
