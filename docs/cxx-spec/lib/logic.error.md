# 19.2.2 Class `logic_error` [logic.error]

[Contents](../README.md) · up: [19.2 Exception classes \[std.exceptions\]](std.exceptions.md) · prev: [19.2.1 Header `<stdexcept>` synopsis \[stdexcept.syn\]](stdexcept.syn.md) · next: [19.2.3 Class `domain_error` \[domain.error\]](domain.error.md)

```cpp
namespace std {
  class logic_error : public exception {
  public:
    explicit logic_error(const string& what_arg);
    explicit logic_error(const char* what_arg);
  };
}
```

**1** The class `logic_error` defines the type of objects thrown as exceptions to report errors presumably detectable before the program executes, such as violations of logical preconditions or class invariants.

```cpp
logic_error(const string& what_arg);
```

**2** *Postconditions:* `strcmp(what(), what_arg.c_str()) == 0`.

```cpp
logic_error(const char* what_arg);
```

**3** *Postconditions:* `strcmp(what(), what_arg) == 0`.
