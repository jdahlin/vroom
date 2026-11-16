# 17.12.2.1 Class template `coroutine_traits` [coroutine.traits.primary]

[Contents](../../README.md) · up: [17.12.2 Coroutine traits \[coroutine.traits\]](coroutine.traits.md) · prev: [17.12.2 Coroutine traits \[coroutine.traits\]](coroutine.traits.md) · next: [17.12.3 Class template `coroutine_handle` \[coroutine.handle\]](coroutine.handle.md)

**1** The header `<coroutine>` defines the primary template `coroutine_traits` such that if `ArgTypes` is a parameter pack of types and if the *qualified-id* `R::promise_type` is valid and denotes a type ([[temp.deduct]](../../temp/temp.deduct.md)), then `coroutine_traits<R,ArgTypes...>` has the following publicly accessible member:

```cpp
using promise_type = typename R::promise_type;
```

Otherwise, `coroutine_traits<R,ArgTypes...>` has no members.

**2** Program-defined specializations of this template shall define a publicly accessible nested type named `promise_type`.
