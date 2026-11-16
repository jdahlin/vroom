# 20.20.3 Error reporting [format.err.report]

[Contents](../README.md) · up: [20.20 Formatting \[format\]](format.md) · prev: [20.20.2.2 Standard format specifiers \[format.string.std\]](format.string.std.md) · next: [20.20.4 Formatting functions \[format.functions\]](format.functions.md)

**1** Formatting functions throw `format_error` if an argument `fmt` is passed that is not a format string for `args`. They propagate exceptions thrown by operations of `formatter` specializations and iterators. Failure to allocate storage is reported by throwing an exception as described in [[res.on.exception.handling]](res.on.exception.handling.md).
