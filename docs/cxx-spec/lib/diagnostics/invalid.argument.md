# 19.2.4 Class `invalid_argument` [invalid.argument]

[Contents](../../README.md) · up: [19.2 Exception classes \[std.exceptions\]](std.exceptions.md) · prev: [19.2.3 Class `domain_error` \[domain.error\]](domain.error.md) · next: [19.2.5 Class `length_error` \[length.error\]](length.error.md)

```cpp
namespace std {
  class invalid_argument : public logic_error {
  public:
    explicit invalid_argument(const string& what_arg);
    explicit invalid_argument(const char* what_arg);
  };
}
```

**1** The class `invalid_argument` defines the type of objects thrown as exceptions to report an invalid argument.

```cpp
invalid_argument(const string& what_arg);
```

**2** *Postconditions:* `strcmp(what(), what_arg.c_str()) == 0`.

```cpp
invalid_argument(const char* what_arg);
```

**3** *Postconditions:* `strcmp(what(), what_arg) == 0`.
