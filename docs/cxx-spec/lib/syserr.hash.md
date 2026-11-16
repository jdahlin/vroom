# 19.5.6 System error hash support [syserr.hash]

[Contents](../README.md) · up: [19.5 System error support \[syserr\]](syserr.md) · prev: [19.5.5 Comparison functions \[syserr.compare\]](syserr.compare.md) · next: [19.5.7 Class `system_error` \[syserr.syserr\]](syserr.syserr.md)

```cpp
template<> struct hash<error_code>;
template<> struct hash<error_condition>;
```

**1** The specializations are enabled ([[unord.hash]](unord.hash.md)).
