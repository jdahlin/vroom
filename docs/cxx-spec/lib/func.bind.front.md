# 20.14.13 Function template `bind_front` [func.bind.front]

[Contents](../README.md) · up: [20.14 Function objects \[function.objects\]](function.objects.md) · prev: [20.14.12 Function template `not_fn` \[func.not.fn\]](func.not.fn.md) · next: [20.14.14 Function object binders \[func.bind\]](func.bind.md)

```cpp
template<class F, class... Args>
  constexpr unspecified bind_front(F&& f, Args&&... args);
```

**1** In the text that follows:

- `g` is a value of the result of a `bind_front` invocation,
- `FD` is the type `decay_t<F>`,
- `fd` is the target object of `g` ([[func.def]](func.def.md)) of type `FD`, direct-non-list-initialized with `std::forward<F>(f)`,
- `BoundArgs` is a pack that denotes `decay_t<Args>...`,
- `bound_args` is a pack of bound argument entities of `g` ([[func.def]](func.def.md)) of types `BoundArgs...`, direct-non-list-initialized with `std::forward<Args>(args)...`, respectively, and
- `call_args` is an argument pack used in a function call expression ([[expr.call]](../expr.call.md)) of `g`.

**2** *Mandates:*

```cpp
is_constructible_v<FD, F> &&
is_move_constructible_v<FD> &&
(is_constructible_v<BoundArgs, Args> && ...) &&
(is_move_constructible_v<BoundArgs> && ...)
```

is `true`.

**3** *Preconditions:* `FD` meets the *Cpp17MoveConstructible* requirements. For each `T`<sub>*i*</sub> in `BoundArgs`, if `T`<sub>*i*</sub> is an object type, `T`<sub>*i*</sub> meets the *Cpp17MoveConstructible* requirements.

**4** *Returns:* A perfect forwarding call wrapper `g` with call pattern `invoke(fd, bound_args..., call_args...)`.

**5** *Throws:* Any exception thrown by the initialization of the state entities of `g` ([[func.def]](func.def.md)).
