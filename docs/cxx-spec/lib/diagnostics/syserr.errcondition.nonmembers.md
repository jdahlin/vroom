# 19.5.4.5 Non-member functions [syserr.errcondition.nonmembers]

[Contents](../../README.md) · up: [19.5.4 Class `error_condition` \[syserr.errcondition\]](syserr.errcondition.md) · prev: [19.5.4.4 Observers \[syserr.errcondition.observers\]](syserr.errcondition.observers.md) · next: [19.5.5 Comparison functions \[syserr.compare\]](syserr.compare.md)

```cpp
error_condition make_error_condition(errc e) noexcept;
```

**1** *Returns:* `error_condition(static_cast<int>(e), generic_category())`.
