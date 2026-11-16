# 32.4.2.4 Assignment [thread.thread.assign]

[Contents](../../README.md) · up: [32.4.2 Class `thread` \[thread.thread.class\]](thread.thread.class.md) · prev: [32.4.2.3 Destructor \[thread.thread.destr\]](thread.thread.destr.md) · next: [32.4.2.5 Members \[thread.thread.member\]](thread.thread.member.md)

```cpp
thread& operator=(thread&& x) noexcept;
```

**1** *Effects:* If `joinable()`, calls `terminate()`. Otherwise, assigns the state of `x` to `*this` and sets `x` to a default constructed state.

**2** *Postconditions:* `x.get_id() == id()` and `get_id()` returns the value of `x.get_id()` prior to the assignment.

**3** *Returns:* `*this`.
