# 19.5.2.2 Virtual members [syserr.errcat.virtuals]

[Contents](../../README.md) · up: [19.5.2 Class `error_category` \[syserr.errcat\]](syserr.errcat.md) · prev: [19.5.2.1 Overview \[syserr.errcat.overview\]](syserr.errcat.overview.md) · next: [19.5.2.3 Non-virtual members \[syserr.errcat.nonvirtuals\]](syserr.errcat.nonvirtuals.md)

```cpp
virtual const char* name() const noexcept = 0;
```

**1** *Returns:* A string naming the error category.

```cpp
virtual error_condition default_error_condition(int ev) const noexcept;
```

**2** *Returns:* `error_condition(ev, *this)`.

```cpp
virtual bool equivalent(int code, const error_condition& condition) const noexcept;
```

**3** *Returns:* `default_error_condition(code) == condition`.

```cpp
virtual bool equivalent(const error_code& code, int condition) const noexcept;
```

**4** *Returns:* `*this == code.category() && code.value() == condition`.

```cpp
virtual string message(int ev) const = 0;
```

**5** *Returns:* A string that describes the error condition denoted by `ev`.
