# 32.4.3.4 Specialized algorithms [thread.jthread.special]

[Contents](../README.md) · up: [32.4.3 Class `jthread` \[thread.jthread.class\]](thread.jthread.class.md) · prev: [32.4.3.3 Stop token handling \[thread.jthread.stop\]](thread.jthread.stop.md) · next: [32.4.3.5 Static members \[thread.jthread.static\]](thread.jthread.static.md)

```cpp
friend void swap(jthread& x, jthread& y) noexcept;
```

**1** *Effects:* Equivalent to: `x.swap(y)`.
