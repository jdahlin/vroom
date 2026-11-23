# 17.6.5 Hardware interference size [hardware.interference]

[Contents](../../README.md) · up: [17.6 Dynamic memory management \[support.dynamic\]](support.dynamic.md) · prev: [17.6.4 Pointer optimization barrier \[ptr.launder\]](ptr.launder.md) · next: [17.7 Type identification \[support.rtti\]](support.rtti.md)

```cpp
inline constexpr size_t hardware_destructive_interference_size = implementation-defined;
```

**1** This number is the minimum recommended offset between two concurrently-accessed objects to avoid additional performance degradation due to contention introduced by the implementation. It shall be at least `alignof(max_align_t)`.

[*Example 1*:

```cpp
struct keep_apart {
  alignas(hardware_destructive_interference_size) atomic<int> cat;
  alignas(hardware_destructive_interference_size) atomic<int> dog;
};
```

— *end example*]

```cpp
inline constexpr size_t hardware_constructive_interference_size = implementation-defined;
```

**2** This number is the maximum recommended size of contiguous memory occupied by two objects accessed with temporal locality by concurrent threads. It shall be at least `alignof(max_align_t)`.

[*Example 2*:

```cpp
struct together {
  atomic<int> dog;
  int puppy;
};
struct kennel {
  // Other data members...
  alignas(sizeof(together)) together pack;
  // Other data members...
};
static_assert(sizeof(together) <= hardware_constructive_interference_size);
```

— *end example*]
