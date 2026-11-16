# 19.5.3.5 Non-member functions [syserr.errcode.nonmembers]

[Contents](../../README.md) · up: [19.5.3 Class `error_code` \[syserr.errcode\]](syserr.errcode.md) · prev: [19.5.3.4 Observers \[syserr.errcode.observers\]](syserr.errcode.observers.md) · next: [19.5.4 Class `error_condition` \[syserr.errcondition\]](syserr.errcondition.md)

```cpp
error_code make_error_code(errc e) noexcept;
```

**1** *Returns:* `error_code(static_cast<int>(e), generic_category())`.

```cpp
template<class charT, class traits>
  basic_ostream<charT, traits>& operator<<(basic_ostream<charT, traits>& os, const error_code& ec);
```

**2** *Effects:* Equivalent to: `return os << ec.category().name() << ':' << ec.value();`
