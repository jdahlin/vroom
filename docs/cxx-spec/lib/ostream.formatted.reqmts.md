# 29.7.5.2.1 Common requirements [ostream.formatted.reqmts]

[Contents](../README.md) · up: [29.7.5.2 Formatted output functions \[ostream.formatted\]](ostream.formatted.md) · prev: [29.7.5.2 Formatted output functions \[ostream.formatted\]](ostream.formatted.md) · next: [29.7.5.2.2 Arithmetic inserters \[ostream.inserters.arithmetic\]](ostream.inserters.arithmetic.md)

**1** Each formatted output function begins execution by constructing an object of class `sentry`. If this object returns `true` when converted to a value of type `bool`, the function endeavors to generate the requested output. If the generation fails, then the formatted output function does `setstate(ios_base::failbit)`, which might throw an exception. If an exception is thrown during output, then `ios_base::badbit` is turned on[^1] in `*this`’s error state. If `(exceptions()&badbit) != 0` then the exception is rethrown. Whether or not an exception is thrown, the `sentry` object is destroyed before leaving the formatted output function. If no exception is thrown, the result of the formatted output function is `*this`.

**2** The descriptions of the individual formatted output functions describe how they perform output and do not mention the `sentry` object.

**3** If a formatted output function of a stream `os` determines padding, it does so as follows. Given a `charT` character sequence `seq` where `charT` is the character type of the stream, if the length of `seq` is less than `os.width()`, then enough copies of `os.fill()` are added to this sequence as necessary to pad to a width of `os.width()` characters. If `(os.flags() & ios_base::adjustfield) == ios_base::left` is `true`, the fill characters are placed after the character sequence; otherwise, they are placed before the character sequence.

[^1]: without causing an `ios_base::failure` to be thrown.
