# 19.2.3 Class `domain_error` [domain.error]

[Contents](../README.md) · up: [19.2 Exception classes \[std.exceptions\]](std.exceptions.md) · prev: [19.2.2 Class `logic_error` \[logic.error\]](logic.error.md) · next: [19.2.4 Class `invalid_argument` \[invalid.argument\]](invalid.argument.md)

```cpp
namespace std {
  class domain_error : public logic_error {
  public:
    explicit domain_error(const string& what_arg);
    explicit domain_error(const char* what_arg);
  };
}
```

**1** The class `domain_error` defines the type of objects thrown as exceptions by the implementation to report domain errors.

```cpp
domain_error(const string& what_arg);
```

**2** *Postconditions:* `strcmp(what(), what_arg.c_str()) == 0`.

```cpp
domain_error(const char* what_arg);
```

**3** *Postconditions:* `strcmp(what(), what_arg) == 0`.
