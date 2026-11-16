# 20.20.7 Class `format_error` [format.error]

[Contents](../../README.md) · up: [20.20 Formatting \[format\]](format.md) · prev: [20.20.6.3 Class template `basic_format_args` \[format.args\]](format.args.md) · next: [21 Strings library \[strings\]](../strings/strings.md)

```cpp
namespace std {
  class format_error : public runtime_error {
  public:
    explicit format_error(const string& what_arg);
    explicit format_error(const char* what_arg);
  };
}
```

**1** The class `format_error` defines the type of objects thrown as exceptions to report errors from the formatting library.

```cpp
format_error(const string& what_arg);
```

**2** *Postconditions:* `strcmp(what(), what_arg.c_str()) == 0`.

```cpp
format_error(const char* what_arg);
```

**3** *Postconditions:* `strcmp(what(), what_arg) == 0`.
