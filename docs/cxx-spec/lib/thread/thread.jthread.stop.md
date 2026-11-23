# 32.4.3.3 Stop token handling [thread.jthread.stop]

[Contents](../../README.md) · up: [32.4.3 Class `jthread` \[thread.jthread.class\]](thread.jthread.class.md) · prev: [32.4.3.2 Members \[thread.jthread.mem\]](thread.jthread.mem.md) · next: [32.4.3.4 Specialized algorithms \[thread.jthread.special\]](thread.jthread.special.md)

```cpp
[[nodiscard]] stop_source get_stop_source() noexcept;
```

**1** *Effects:* Equivalent to: `return ssource;`

```cpp
[[nodiscard]] stop_token get_stop_token() const noexcept;
```

**2** *Effects:* Equivalent to: `return ssource.get_token();`

```cpp
bool request_stop() noexcept;
```

**3** *Effects:* Equivalent to: `return ssource.request_stop();`
