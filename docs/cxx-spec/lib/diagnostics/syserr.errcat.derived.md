# 19.5.2.4 Program-defined classes derived from `error_category` [syserr.errcat.derived]

[Contents](../../README.md) · up: [19.5.2 Class `error_category` \[syserr.errcat\]](syserr.errcat.md) · prev: [19.5.2.3 Non-virtual members \[syserr.errcat.nonvirtuals\]](syserr.errcat.nonvirtuals.md) · next: [19.5.2.5 Error category objects \[syserr.errcat.objects\]](syserr.errcat.objects.md)

```cpp
virtual const char* name() const noexcept = 0;
```

**1** *Returns:* A string naming the error category.

```cpp
virtual error_condition default_error_condition(int ev) const noexcept;
```

**2** *Returns:* An object of type `error_condition` that corresponds to `ev`.

```cpp
virtual bool equivalent(int code, const error_condition& condition) const noexcept;
```

**3** *Returns:* `true` if, for the category of error represented by `*this`, `code` is considered equivalent to `condition`; otherwise, `false`.

```cpp
virtual bool equivalent(const error_code& code, int condition) const noexcept;
```

**4** *Returns:* `true` if, for the category of error represented by `*this`, `code` is considered equivalent to `condition`; otherwise, `false`.
