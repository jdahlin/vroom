# 20.6.3.6 Modifiers [optional.mod]

[Contents](../README.md) · up: [20.6.3 Class template `optional` \[optional.optional\]](optional.optional.md) · prev: [20.6.3.5 Observers \[optional.observe\]](optional.observe.md) · next: [20.6.4 No-value state indicator \[optional.nullopt\]](optional.nullopt.md)

```cpp
void reset() noexcept;
```

**1** *Effects:* If `*this` contains a value, calls `val->T::~T()` to destroy the contained value; otherwise no effect.

**2** *Postconditions:* `*this` does not contain a value.
