# 19.5.2.5 Error category objects [syserr.errcat.objects]

[Contents](../../README.md) · up: [19.5.2 Class `error_category` \[syserr.errcat\]](syserr.errcat.md) · prev: [19.5.2.4 Program-defined classes derived from `error_category` \[syserr.errcat.derived\]](syserr.errcat.derived.md) · next: [19.5.3 Class `error_code` \[syserr.errcode\]](syserr.errcode.md)

```cpp
const error_category& generic_category() noexcept;
```

**1** *Returns:* A reference to an object of a type derived from class `error_category`. All calls to this function shall return references to the same object.

**2** *Remarks:* The object’s `default_error_condition` and `equivalent` virtual functions shall behave as specified for the class `error_category`. The object’s `name` virtual function shall return a pointer to the string `"generic"`.

```cpp
const error_category& system_category() noexcept;
```

**3** *Returns:* A reference to an object of a type derived from class `error_category`. All calls to this function shall return references to the same object.

**4** *Remarks:* The object’s `equivalent` virtual functions shall behave as specified for class `error_category`. The object’s `name` virtual function shall return a pointer to the string `"system"`. The object’s `default_error_condition` virtual function shall behave as follows:

If the argument `ev` corresponds to a POSIX `errno` value `posv`, the function shall return `error_condition(posv, generic_category())`. Otherwise, the function shall return `error_condition(ev, system_category())`. What constitutes correspondence for any given operating system is unspecified. [*Note 1*: The number of potential system error codes is large and unbounded, and some may not correspond to any POSIX `errno` value. Thus implementations are given latitude in determining correspondence. — *end note*]
