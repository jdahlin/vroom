# 32.3.3.1 Constructors, copy, and assignment [stoptoken.cons]

[Contents](../README.md) · up: [32.3.3 Class `stop_token` \[stoptoken\]](stoptoken.md) · prev: [32.3.3 Class `stop_token` \[stoptoken\]](stoptoken.md) · next: [32.3.3.2 Members \[stoptoken.mem\]](stoptoken.mem.md)

```cpp
stop_token() noexcept;
```

**1** *Postconditions:* `stop_possible()` is `false` and `stop_requested()` is `false`. [*Note 1*: Because the created `stop_token` object can never receive a stop request, no resources are allocated for a stop state. — *end note*]

```cpp
stop_token(const stop_token& rhs) noexcept;
```

**2** *Postconditions:* `*this == rhs` is `true`. [*Note 2*: `*this` and `rhs` share the ownership of the same stop state, if any. — *end note*]

```cpp
stop_token(stop_token&& rhs) noexcept;
```

**3** *Postconditions:* `*this` contains the value of `rhs` prior to the start of construction and `rhs.stop_possible()` is `false`.

```cpp
~stop_token();
```

**4** *Effects:* Releases ownership of the stop state, if any.

```cpp
stop_token& operator=(const stop_token& rhs) noexcept;
```

**5** *Effects:* Equivalent to: `stop_token(rhs).swap(*this)`.

**6** *Returns:* `*this`.

```cpp
stop_token& operator=(stop_token&& rhs) noexcept;
```

**7** *Effects:* Equivalent to: `stop_token(std::move(rhs)).swap(*this)`.

**8** *Returns:* `*this`.

```cpp
void swap(stop_token& rhs) noexcept;
```

**9** *Effects:* Exchanges the values of `*this` and `rhs`.
