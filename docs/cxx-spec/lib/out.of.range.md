# 19.2.6 Class `out_of_range` [out.of.range]

[Contents](../README.md) · up: [19.2 Exception classes \[std.exceptions\]](std.exceptions.md) · prev: [19.2.5 Class `length_error` \[length.error\]](length.error.md) · next: [19.2.7 Class `runtime_error` \[runtime.error\]](runtime.error.md)

```cpp
namespace std {
  class out_of_range : public logic_error {
  public:
    explicit out_of_range(const string& what_arg);
    explicit out_of_range(const char* what_arg);
  };
}
```

**1** The class `out_of_range` defines the type of objects thrown as exceptions to report an argument value not in its expected range.

```cpp
out_of_range(const string& what_arg);
```

**2** *Postconditions:* `strcmp(what(), what_arg.c_str()) == 0`.

```cpp
out_of_range(const char* what_arg);
```

**3** *Postconditions:* `strcmp(what(), what_arg) == 0`.
