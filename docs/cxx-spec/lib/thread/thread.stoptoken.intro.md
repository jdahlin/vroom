# 32.3.1 Introduction [thread.stoptoken.intro]

[Contents](../../README.md) · up: [32.3 Stop tokens \[thread.stoptoken\]](thread.stoptoken.md) · prev: [32.3 Stop tokens \[thread.stoptoken\]](thread.stoptoken.md) · next: [32.3.2 Header `<stop_token>` synopsis \[thread.stoptoken.syn\]](thread.stoptoken.syn.md)

**1** This clause describes components that can be used to asynchonously request that an operation stops execution in a timely manner, typically because the result is no longer required. Such a request is called a *stop request*.

**2** `stop_source`, `stop_token`, and `stop_callback` implement semantics of shared ownership of a *stop state*. Any `stop_source`, `stop_token`, or `stop_callback` that shares ownership of the same stop state is an *associated* `stop_source`, `stop_token`, or `stop_callback`, respectively. The last remaining owner of the stop state automatically releases the resources associated with the stop state.

**3** A `stop_token` can be passed to an operation which can either

- actively poll the token to check if there has been a stop request, or
- register a callback using the `stop_callback` class template which will be called in the event that a stop request is made.

A stop request made via a `stop_source` will be visible to all associated `stop_token` and `stop_source` objects. Once a stop request has been made it cannot be withdrawn (a subsequent stop request has no effect).

**4** Callbacks registered via a `stop_callback` object are called when a stop request is first made by any associated `stop_source` object.

**5** Calls to the functions `request_stop`, `stop_requested`, and `stop_possible` do not introduce data races. A call to `request_stop` that returns `true` synchronizes with a call to `stop_requested` on an associated `stop_token` or `stop_source` object that returns `true`. Registration of a callback synchronizes with the invocation of that callback.
