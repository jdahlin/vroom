# 32.4.2.3 Destructor [thread.thread.destr]

[Contents](../README.md) · up: [32.4.2 Class `thread` \[thread.thread.class\]](thread.thread.class.md) · prev: [32.4.2.2 Constructors \[thread.thread.constr\]](thread.thread.constr.md) · next: [32.4.2.4 Assignment \[thread.thread.assign\]](thread.thread.assign.md)

```cpp
~thread();
```

**1** *Effects:* If `joinable()`, calls `terminate()`. Otherwise, has no effects. [*Note 1*: Either implicitly detaching or joining a `joinable()` thread in its destructor could result in difficult to debug correctness (for detach) or performance (for join) bugs encountered only when an exception is thrown. Thus the programmer must ensure that the destructor is never executed while the thread is still joinable. — *end note*]
