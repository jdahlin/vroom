# 20.3.2 Class template `integer_sequence` [intseq.intseq]

[Contents](../../README.md) · up: [20.3 Compile-time integer sequences \[intseq\]](intseq.md) · prev: [20.3.1 In general \[intseq.general\]](intseq.general.md) · next: [20.3.3 Alias template `make_integer_sequence` \[intseq.make\]](intseq.make.md)

```cpp
namespace std {
  template<class T, T... I> struct integer_sequence {
    using value_type = T;
    static constexpr size_t size() noexcept { return sizeof...(I); }
  };
}
```

**1** *Mandates:* `T` is an integer type.
