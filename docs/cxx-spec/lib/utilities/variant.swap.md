# 20.7.3.6 Swap [variant.swap]

[Contents](../../README.md) · up: [20.7.3 Class template `variant` \[variant.variant\]](variant.variant.md) · prev: [20.7.3.5 Value status \[variant.status\]](variant.status.md) · next: [20.7.4 `variant` helper classes \[variant.helper\]](variant.helper.md)

```cpp
void swap(variant& rhs) noexcept(see below);
```

**1** *Mandates:* `is_move_constructible_v<T_i>` is `true` for all *i*.

**2** *Preconditions:* Lvalues of type `T`<sub>*i*</sub> are swappable ([[swappable.requirements]](../library/swappable.requirements.md)).

**3** *Effects:*

- If `valueless_by_exception() && rhs.valueless_by_exception()` no effect.
- Otherwise, if `index() == rhs.index()`, calls `swap(get<i>(*this), get<i>(rhs))` where *i* is `index()`.
- Otherwise, exchanges values of `rhs` and `*this`.

**4** *Throws:* If `index() == rhs.index()`, any exception thrown by `swap(get<i>(*this), get<i>(rhs))` with *i* being `index()`. Otherwise, any exception thrown by the move constructor of `T`<sub>*i*</sub> or `T`<sub>*j*</sub> with *i* being `index()` and *j* being `rhs.index()`.

**5** *Remarks:* If an exception is thrown during the call to function `swap(get<i>(*this), get<i>(rhs))`, the states of the contained values of `*this` and of `rhs` are determined by the exception safety guarantee of `swap` for lvalues of `T`<sub>*i*</sub> with *i* being `index()`. If an exception is thrown during the exchange of the values of `*this` and `rhs`, the states of the values of `*this` and of `rhs` are determined by the exception safety guarantee of `variant`’s move constructor. The expression inside `noexcept` is equivalent to the logical AND of `is_nothrow_move_constructible_v<T_i> && is_nothrow_swappable_v<T_i>` for all *i*.
