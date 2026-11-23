# 32.4.2.6 Static members [thread.thread.static]

[Contents](../../README.md) · up: [32.4.2 Class `thread` \[thread.thread.class\]](thread.thread.class.md) · prev: [32.4.2.5 Members \[thread.thread.member\]](thread.thread.member.md) · next: [32.4.2.7 Specialized algorithms \[thread.thread.algorithm\]](thread.thread.algorithm.md)

```cpp
unsigned hardware_concurrency() noexcept;
```

**1** *Returns:* The number of hardware thread contexts. [*Note 1*: This value should only be considered to be a hint. — *end note*] If this value is not computable or well-defined, an implementation should return 0.
