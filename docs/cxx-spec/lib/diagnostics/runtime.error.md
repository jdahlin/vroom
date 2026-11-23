# 19.2.7 Class `runtime_error` [runtime.error]

[Contents](../../README.md) · up: [19.2 Exception classes \[std.exceptions\]](std.exceptions.md) · prev: [19.2.6 Class `out_of_range` \[out.of.range\]](out.of.range.md) · next: [19.2.8 Class `range_error` \[range.error\]](range.error.md)

```cpp
namespace std {
  class runtime_error : public exception {
  public:
    explicit runtime_error(const string& what_arg);
    explicit runtime_error(const char* what_arg);
  };
}
```

**1** The class `runtime_error` defines the type of objects thrown as exceptions to report errors presumably detectable only when the program executes.

```cpp
runtime_error(const string& what_arg);
```

**2** *Postconditions:* `strcmp(what(), what_arg.c_str()) == 0`.

```cpp
runtime_error(const char* what_arg);
```

**3** *Postconditions:* `strcmp(what(), what_arg) == 0`.
