# 29.5.3.1.1 Class `ios_base::failure` [ios.failure]

[Contents](../README.md) · up: [29.5.3.1 Types \[ios.types\]](ios.types.md) · prev: [29.5.3.1 Types \[ios.types\]](ios.types.md) · next: [29.5.3.1.2 Type `ios_base::fmtflags` \[ios.fmtflags\]](ios.fmtflags.md)

```cpp
namespace std {
  class ios_base::failure : public system_error {
  public:
    explicit failure(const string& msg, const error_code& ec = io_errc::stream);
    explicit failure(const char* msg, const error_code& ec = io_errc::stream);
  };
}
```

**1** An implementation is permitted to define `ios_base::failure` as a synonym for a class with equivalent functionality to class `ios_base::failure` shown in this subclause. [*Note 1*: When `ios_base::failure` is a synonym for another type, that type is required to provide a nested type `failure` to emulate the injected-class-name. — *end note*] The class `failure` defines the base class for the types of all objects thrown as exceptions, by functions in the iostreams library, to report errors detected during stream buffer operations.

**2** When throwing `ios_base::failure` exceptions, implementations should provide values of `ec` that identify the specific reason for the failure. [*Note 2*: Errors arising from the operating system would typically be reported as `system_category()` errors with an error value of the error number reported by the operating system. Errors arising from within the stream library would typically be reported as `error_code(io_errc::stream, iostream_category())`. — *end note*]

```cpp
explicit failure(const string& msg, const error_code& ec = io_errc::stream);
```

**3** *Effects:* Constructs the base class with `msg` and `ec`.

```cpp
explicit failure(const char* msg, const error_code& ec = io_errc::stream);
```

**4** *Effects:* Constructs the base class with `msg` and `ec`.
