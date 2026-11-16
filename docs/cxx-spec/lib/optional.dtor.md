# 20.6.3.2 Destructor [optional.dtor]

[Contents](../README.md) · up: [20.6.3 Class template `optional` \[optional.optional\]](optional.optional.md) · prev: [20.6.3.1 Constructors \[optional.ctor\]](optional.ctor.md) · next: [20.6.3.3 Assignment \[optional.assign\]](optional.assign.md)

```cpp
~optional();
```

**1** *Effects:* If `is_trivially_destructible_v<T> != true` and `*this` contains a value, calls

```cpp
val->T::~T()
```

**2** *Remarks:* If `is_trivially_destructible_v<T>` is `true`, then this destructor is trivial.
