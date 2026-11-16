# 32.3.4.1 Constructors, copy, and assignment [stopsource.cons]

[Contents](../../README.md) · up: [32.3.4 Class `stop_source` \[stopsource\]](stopsource.md) · prev: [32.3.4 Class `stop_source` \[stopsource\]](stopsource.md) · next: [32.3.4.2 Members \[stopsource.mem\]](stopsource.mem.md)

```cpp
stop_source();
```

**1** *Effects:* Initialises `*this` to have ownership of a new stop state.

**2** *Postconditions:* `stop_possible()` is `true` and `stop_requested()` is `false`.

**3** *Throws:* `bad_alloc` if memory could not be allocated for the stop state.

```cpp
explicit stop_source(nostopstate_t) noexcept;
```

**4** *Postconditions:* `stop_possible()` is `false` and `stop_requested()` is `false`. [*Note 1*: No resources are allocated for the state. — *end note*]

```cpp
stop_source(const stop_source& rhs) noexcept;
```

**5** *Postconditions:* `*this == rhs` is `true`. [*Note 2*: `*this` and `rhs` share the ownership of the same stop state, if any. — *end note*]

```cpp
stop_source(stop_source&& rhs) noexcept;
```

**6** *Postconditions:* `*this` contains the value of `rhs` prior to the start of construction and `rhs.stop_possible()` is `false`.

```cpp
~stop_source();
```

**7** *Effects:* Releases ownership of the stop state, if any.

```cpp
stop_source& operator=(const stop_source& rhs) noexcept;
```

**8** *Effects:* Equivalent to: `stop_source(rhs).swap(*this)`.

**9** *Returns:* `*this`.

```cpp
stop_source& operator=(stop_source&& rhs) noexcept;
```

**10** *Effects:* Equivalent to: `stop_source(std::move(rhs)).swap(*this)`.

**11** *Returns:* `*this`.

```cpp
void swap(stop_source& rhs) noexcept;
```

**12** *Effects:* Exchanges the values of `*this` and `rhs`.
