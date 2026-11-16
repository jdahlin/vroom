# 29.7.4.2.1 Common requirements [istream.formatted.reqmts]

[Contents](../README.md) · up: [29.7.4.2 Formatted input functions \[istream.formatted\]](istream.formatted.md) · prev: [29.7.4.2 Formatted input functions \[istream.formatted\]](istream.formatted.md) · next: [29.7.4.2.2 Arithmetic extractors \[istream.formatted.arithmetic\]](istream.formatted.arithmetic.md)

**1** Each formatted input function begins execution by constructing an object of class `sentry` with the `noskipws` (second) argument `false`. If the `sentry` object returns `true`, when converted to a value of type `bool`, the function endeavors to obtain the requested input. If an exception is thrown during input then `ios_base::badbit` is turned on[^1] in `*this`’s error state. If `(exceptions()&badbit) != 0` then the exception is rethrown. In any case, the formatted input function destroys the `sentry` object. If no exception has been thrown, it returns `*this`.

[^1]: This is done without causing an `ios_base::failure` to be thrown.
