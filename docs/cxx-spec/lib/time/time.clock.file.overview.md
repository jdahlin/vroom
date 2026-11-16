# 27.7.5.1 Overview [time.clock.file.overview]

[Contents](../../README.md) · up: [27.7.5 Type `file_clock` \[time.clock.file\]](time.clock.file.md) · prev: [27.7.5 Type `file_clock` \[time.clock.file\]](time.clock.file.md) · next: [27.7.5.2 Member functions \[time.clock.file.members\]](time.clock.file.members.md)

```cpp
namespace std::chrono {
  using file_clock = see below;
}
```

**1** `file_clock` is an alias for a type meeting the *Cpp17TrivialClock* requirements ([[time.clock.req]](time.clock.req.md)), and using a signed arithmetic type for `file_clock::rep`. `file_clock` is used to create the `time_point` system used for `file_time_type` ([[filesystems]](../io/filesystems.md)). Its epoch is unspecified, and `noexcept(file_clock::now())` is `true`. [*Note 1*: The type that `file_clock` denotes may be in a different namespace than `std::chrono`, such as `std::filesystem`. — *end note*]
