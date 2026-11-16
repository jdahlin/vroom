# 29.7.4.1.3 Class `basic_istream::sentry` [istream.sentry]

[Contents](../README.md) · up: [29.7.4.1 Class template `basic_istream` \[istream\]](istream.md) · prev: [29.7.4.1.2 Assignment and swap \[istream.assign\]](istream.assign.md) · next: [29.7.4.2 Formatted input functions \[istream.formatted\]](istream.formatted.md)

```cpp
namespace std {
  template<class charT, class traits = char_traits<charT>>
  class basic_istream<charT, traits>::sentry {
    bool ok_;                   // exposition only
  public:
    explicit sentry(basic_istream<charT, traits>& is, bool noskipws = false);
    ~sentry();
    explicit operator bool() const { return ok_; }
    sentry(const sentry&) = delete;
    sentry& operator=(const sentry&) = delete;
  };
}
```

**1** The class `sentry` defines a class that is responsible for doing exception safe prefix and suffix operations.

```cpp
explicit sentry(basic_istream<charT, traits>& is, bool noskipws = false);
```

**2** *Effects:* If `is.good()` is `false`, calls `is.setstate(failbit)`. Otherwise, prepares for formatted or unformatted input. First, if `is.tie()` is not a null pointer, the function calls `is.tie()->flush()` to synchronize the output sequence with any associated external C stream. Except that this call can be suppressed if the put area of `is.tie()` is empty. Further an implementation is allowed to defer the call to `flush` until a call of `is.rdbuf()->underflow()` occurs. If no such call occurs before the `sentry` object is destroyed, the call to `flush` may be eliminated entirely.[^1] If `noskipws` is zero and `is.flags() & ios_base::skipws` is nonzero, the function extracts and discards each character as long as the next available input character `c` is a whitespace character. If `is.rdbuf()->sbumpc()` or `is.rdbuf()->sgetc()` returns `traits::eof()`, the function calls `setstate(failbit | eofbit)` (which may throw `ios_base::failure`).

**3** *Remarks:* The constructor

```cpp
explicit sentry(basic_istream<charT, traits>& is, bool noskipws = false)
```

uses the currently imbued locale in `is`, to determine whether the next input character is whitespace or not.

**4** To decide if the character `c` is a whitespace character, the constructor performs as if it executes the following code fragment:

```cpp
const ctype<charT>& ctype = use_facet<ctype<charT>>(is.getloc());
if (ctype.is(ctype.space, c) != 0)
  // c is a whitespace character.
```

**5** If, after any preparation is completed, `is.good()` is `true`, `ok_ != false` otherwise, `ok_ == false`. During preparation, the constructor may call `setstate(failbit)` (which may throw `ios_base::failure` ([[iostate.flags]](iostate.flags.md))).[^2]

```cpp
~sentry();
```

**6** *Effects:* None.

```cpp
explicit operator bool() const;
```

**7** *Returns:* `ok_`.

[^1]: This will be possible only in functions that are part of the library. The semantics of the constructor used in user code is as specified.
[^2]: The `sentry` constructor and destructor can also perform additional implementation-dependent operations.
