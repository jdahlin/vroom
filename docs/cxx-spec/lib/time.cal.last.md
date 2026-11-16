# 27.8.2 Class `last_spec` [time.cal.last]

[Contents](../README.md) · up: [27.8 The civil calendar \[time.cal\]](time.cal.md) · prev: [27.8.1 In general \[time.cal.general\]](time.cal.general.md) · next: [27.8.3 Class `day` \[time.cal.day\]](time.cal.day.md)

```cpp
namespace std::chrono {
  struct last_spec {
    explicit last_spec() = default;
  };
}
```

**1** The type `last_spec` is used in conjunction with other calendar types to specify the last in a sequence. For example, depending on context, it can represent the last day of a month, or the last day of the week of a month.
