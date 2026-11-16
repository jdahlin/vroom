# 19.2.9 Class `overflow_error` [overflow.error]

[Contents](../README.md) · up: [19.2 Exception classes \[std.exceptions\]](std.exceptions.md) · prev: [19.2.8 Class `range_error` \[range.error\]](range.error.md) · next: [19.2.10 Class `underflow_error` \[underflow.error\]](underflow.error.md)

```cpp
namespace std {
  class overflow_error : public runtime_error {
  public:
    explicit overflow_error(const string& what_arg);
    explicit overflow_error(const char* what_arg);
  };
}
```

**1** The class `overflow_error` defines the type of objects thrown as exceptions to report an arithmetic overflow error.

```cpp
overflow_error(const string& what_arg);
```

**2** *Postconditions:* `strcmp(what(), what_arg.c_str()) == 0`.

```cpp
overflow_error(const char* what_arg);
```

**3** *Postconditions:* `strcmp(what(), what_arg) == 0`.
