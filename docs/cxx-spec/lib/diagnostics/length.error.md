# 19.2.5 Class `length_error` [length.error]

[Contents](../../README.md) · up: [19.2 Exception classes \[std.exceptions\]](std.exceptions.md) · prev: [19.2.4 Class `invalid_argument` \[invalid.argument\]](invalid.argument.md) · next: [19.2.6 Class `out_of_range` \[out.of.range\]](out.of.range.md)

```cpp
namespace std {
  class length_error : public logic_error {
  public:
    explicit length_error(const string& what_arg);
    explicit length_error(const char* what_arg);
  };
}
```

**1** The class `length_error` defines the type of objects thrown as exceptions to report an attempt to produce an object whose length exceeds its maximum allowable size.

```cpp
length_error(const string& what_arg);
```

**2** *Postconditions:* `strcmp(what(), what_arg.c_str()) == 0`.

```cpp
length_error(const char* what_arg);
```

**3** *Postconditions:* `strcmp(what(), what_arg) == 0`.
