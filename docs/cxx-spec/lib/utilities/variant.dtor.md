# 20.7.3.2 Destructor [variant.dtor]

[Contents](../../README.md) · up: [20.7.3 Class template `variant` \[variant.variant\]](variant.variant.md) · prev: [20.7.3.1 Constructors \[variant.ctor\]](variant.ctor.md) · next: [20.7.3.3 Assignment \[variant.assign\]](variant.assign.md)

```cpp
~variant();
```

**1** *Effects:* If `valueless_by_exception()` is `false`, destroys the currently contained value.

**2** *Remarks:* If `is_trivially_destructible_v<T_i>` is `true` for all `T`<sub>*i*</sub>, then this destructor is trivial.
