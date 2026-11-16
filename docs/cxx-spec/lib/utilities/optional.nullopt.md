# 20.6.4 No-value state indicator [optional.nullopt]

[Contents](../../README.md) · up: [20.6 Optional objects \[optional\]](optional.md) · prev: [20.6.3.6 Modifiers \[optional.mod\]](optional.mod.md) · next: [20.6.5 Class `bad_optional_access` \[optional.bad.access\]](optional.bad.access.md)

```cpp
struct nullopt_t{see below};
inline constexpr nullopt_t nullopt(unspecified);
```

**1** The struct `nullopt_t` is an empty class type used as a unique type to indicate the state of not containing a value for `optional` objects. In particular, `optional<T>` has a constructor with `nullopt_t` as a single argument; this indicates that an optional object not containing a value shall be constructed.

**2** Type `nullopt_t` shall not have a default constructor or an initializer-list constructor, and shall not be an aggregate.
