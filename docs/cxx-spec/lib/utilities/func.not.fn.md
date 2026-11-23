# 20.14.12 Function template `not_fn` [func.not.fn]

[Contents](../../README.md) · up: [20.14 Function objects \[function.objects\]](function.objects.md) · prev: [20.14.11 Class `identity` \[func.identity\]](func.identity.md) · next: [20.14.13 Function template `bind_front` \[func.bind.front\]](func.bind.front.md)

```cpp
template<class F> constexpr unspecified not_fn(F&& f);
```

**1** In the text that follows:

- `g` is a value of the result of a `not_fn` invocation,
- `FD` is the type `decay_t<F>`,
- `fd` is the target object of `g` ([[func.def]](func.def.md)) of type `FD`, direct-non-list-initialized with `std::forward<F>(f)`,
- `call_args` is an argument pack used in a function call expression ([[expr.call]](../../expr/expr.call.md)) of `g`.

**2** *Mandates:* `is_constructible_v<FD, F> && is_move_constructible_v<FD>` is `true`.

**3** *Preconditions:* `FD` meets the *Cpp17MoveConstructible* requirements.

**4** *Returns:* A perfect forwarding call wrapper `g` with call pattern `!invoke(fd, call_args...)`.

**5** *Throws:* Any exception thrown by the initialization of `fd`.
