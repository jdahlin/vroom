# 29.7.5.1.3 Class `basic_ostream::sentry` [ostream.sentry]

[Contents](../../README.md) · up: [29.7.5.1 Class template `basic_ostream` \[ostream\]](ostream.md) · prev: [29.7.5.1.2 Assignment and swap \[ostream.assign\]](ostream.assign.md) · next: [29.7.5.1.4 Seek members \[ostream.seeks\]](ostream.seeks.md)

```cpp
namespace std {
  template<class charT, class traits = char_traits<charT>>
  class basic_ostream<charT, traits>::sentry {
    bool ok_;       // exposition only
  public:
    explicit sentry(basic_ostream<charT, traits>& os);
    ~sentry();
    explicit operator bool() const { return ok_; }

    sentry(const sentry&) = delete;
    sentry& operator=(const sentry&) = delete;
  };
}
```

**1** The class `sentry` defines a class that is responsible for doing exception safe prefix and suffix operations.

```cpp
explicit sentry(basic_ostream<charT, traits>& os);
```

**2** If `os.good()` is nonzero, prepares for formatted or unformatted output. If `os.tie()` is not a null pointer, calls `os.tie()->flush()`.[^1]

**3** If, after any preparation is completed, `os.good()` is `true`, `ok_ == true` otherwise, `ok_ == false`. During preparation, the constructor may call `setstate(failbit)` (which may throw `ios_base::failure` ([[iostate.flags]](iostate.flags.md))).[^2]

```cpp
~sentry();
```

**4** If `(os.flags() & ios_base::unitbuf) && !uncaught_exceptions() && os.good()` is `true`, calls `os.rdbuf()->pubsync()`. If that function returns -1, sets `badbit` in `os.rdstate()` without propagating an exception.

```cpp
explicit operator bool() const;
```

**5** *Effects:* Returns `ok_`.

[^1]: The call `os.tie()->flush()` does not necessarily occur if the function can determine that no synchronization is necessary.
[^2]: The `sentry` constructor and destructor can also perform additional implementation-dependent operations.
