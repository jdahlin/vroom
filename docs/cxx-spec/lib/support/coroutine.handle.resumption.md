# 17.12.3.4 Resumption [coroutine.handle.resumption]

[Contents](../../README.md) · up: [17.12.3 Class template `coroutine_handle` \[coroutine.handle\]](coroutine.handle.md) · prev: [17.12.3.3 Observers \[coroutine.handle.observers\]](coroutine.handle.observers.md) · next: [17.12.3.5 Promise access \[coroutine.handle.promise\]](coroutine.handle.promise.md)

**1** Resuming a coroutine via `resume`, `operator()`, or `destroy` on an execution agent other than the one on which it was suspended has implementation-defined behavior unless each execution agent either is an instance of `std::thread` or `std::jthread`, or is the thread that executes `main`. [*Note 1*: A coroutine that is resumed on a different execution agent should avoid relying on consistent thread identity throughout, such as holding a mutex object across a suspend point. — *end note*] [*Note 2*: A concurrent resumption of the coroutine may result in a data race. — *end note*]

```cpp
void operator()() const;
void resume() const;
```

**2** *Preconditions:* `*this` refers to a suspended coroutine. The coroutine is not suspended at its final suspend point.

**3** *Effects:* Resumes the execution of the coroutine.

```cpp
void destroy() const;
```

**4** *Preconditions:* `*this` refers to a suspended coroutine.

**5** *Effects:* Destroys the coroutine ([[dcl.fct.def.coroutine]](../../dcl/dcl.fct.def.coroutine.md)).
