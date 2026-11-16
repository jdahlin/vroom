# 30.6 Class `regex_error` [re.badexp]

[Contents](../README.md) · up: [30 Regular expressions library \[re\]](re.md) · prev: [30.5.3 Implementation-defined `error_type` \[re.err\]](re.err.md) · next: [30.7 Class template `regex_traits` \[re.traits\]](re.traits.md)

```cpp
class regex_error : public runtime_error {
public:
  explicit regex_error(regex_constants::error_type ecode);
  regex_constants::error_type code() const;
};
```

**1** The class `regex_error` defines the type of objects thrown as exceptions to report errors from the regular expression library.

```cpp
regex_error(regex_constants::error_type ecode);
```

**2** *Postconditions:* `ecode == code()`.

```cpp
regex_constants::error_type code() const;
```

**3** *Returns:* The error code that was passed to the constructor.
