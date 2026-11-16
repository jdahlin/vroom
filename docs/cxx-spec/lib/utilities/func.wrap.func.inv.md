# 20.14.16.2.4 Invocation [func.wrap.func.inv]

[Contents](../../README.md) · up: [20.14.16.2 Class template `function` \[func.wrap.func\]](func.wrap.func.md) · prev: [20.14.16.2.3 Capacity \[func.wrap.func.cap\]](func.wrap.func.cap.md) · next: [20.14.16.2.5 Target access \[func.wrap.func.targ\]](func.wrap.func.targ.md)

```cpp
R operator()(ArgTypes... args) const;
```

**1** *Returns:* `INVOKE<R>(f, std::forward<ArgTypes>(args)...)` ([[func.require]](func.require.md)), where `f` is the target object ([[func.def]](func.def.md)) of `*this`.

**2** *Throws:* `bad_function_call` if `!*this`; otherwise, any exception thrown by the wrapped callable object.
