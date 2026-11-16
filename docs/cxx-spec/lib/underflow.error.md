# 19.2.10 Class `underflow_error` [underflow.error]

[Contents](../README.md) · up: [19.2 Exception classes \[std.exceptions\]](std.exceptions.md) · prev: [19.2.9 Class `overflow_error` \[overflow.error\]](overflow.error.md) · next: [19.3 Assertions \[assertions\]](assertions.md)

```cpp
namespace std {
  class underflow_error : public runtime_error {
  public:
    explicit underflow_error(const string& what_arg);
    explicit underflow_error(const char* what_arg);
  };
}
```

**1** The class `underflow_error` defines the type of objects thrown as exceptions to report an arithmetic underflow error.

```cpp
underflow_error(const string& what_arg);
```

**2** *Postconditions:* `strcmp(what(), what_arg.c_str()) == 0`.

```cpp
underflow_error(const char* what_arg);
```

**3** *Postconditions:* `strcmp(what(), what_arg) == 0`.
