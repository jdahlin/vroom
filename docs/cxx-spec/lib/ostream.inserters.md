# 29.7.5.2.3 `basic_ostream::operator<<` [ostream.inserters]

[Contents](../README.md) · up: [29.7.5.2 Formatted output functions \[ostream.formatted\]](ostream.formatted.md) · prev: [29.7.5.2.2 Arithmetic inserters \[ostream.inserters.arithmetic\]](ostream.inserters.arithmetic.md) · next: [29.7.5.2.4 Character inserter function templates \[ostream.inserters.character\]](ostream.inserters.character.md)

```cpp
basic_ostream<charT, traits>&
  operator<<(basic_ostream<charT, traits>& (*pf)(basic_ostream<charT, traits>&));
```

**1** *Effects:* None. Does not behave as a formatted output function (as described in [[ostream.formatted.reqmts]](ostream.formatted.reqmts.md)).

**2** *Returns:* `pf(*this)`.[^1]

```cpp
basic_ostream<charT, traits>&
  operator<<(basic_ios<charT, traits>& (*pf)(basic_ios<charT, traits>&));
```

**3** *Effects:* Calls `pf(*this)`. This inserter does not behave as a formatted output function (as described in [[ostream.formatted.reqmts]](ostream.formatted.reqmts.md)).

**4** *Returns:* `*this`.[^2]

```cpp
basic_ostream<charT, traits>& operator<<(ios_base& (*pf)(ios_base&));
```

**5** *Effects:* Calls `pf(*this)`. This inserter does not behave as a formatted output function (as described in [[ostream.formatted.reqmts]](ostream.formatted.reqmts.md)).

**6** *Returns:* `*this`.

```cpp
basic_ostream<charT, traits>& operator<<(basic_streambuf<charT, traits>* sb);
```

**7** *Effects:* Behaves as an unformatted output function ([[ostream.unformatted]](ostream.unformatted.md)). After the sentry object is constructed, if `sb` is null calls `setstate(badbit)` (which may throw `ios_base::failure`).

**8** Gets characters from `sb` and inserts them in `*this`. Characters are read from `sb` and inserted until any of the following occurs:

- end-of-file occurs on the input sequence;
- inserting in the output sequence fails (in which case the character to be inserted is not extracted);
- an exception occurs while getting a character from `sb`.

**9** If the function inserts no characters, it calls `setstate(failbit)` (which may throw `ios_base::failure` ([[iostate.flags]](iostate.flags.md))). If an exception was thrown while extracting a character, the function sets `failbit` in the error state, and if `failbit` is set in `exceptions()` the caught exception is rethrown.

**10** *Returns:* `*this`.

```cpp
basic_ostream<charT, traits>& operator<<(nullptr_t);
```

**11** *Effects:* Equivalent to:

```cpp
return *this << s;
```

where `s` is an implementation-defined NTCTS ([[defns.ntcts]](definitions.md#defns.ntcts)).

[^1]: See, for example, the function signature `endl(basic_ostream&)` ([[ostream.manip]](ostream.manip.md)).
[^2]: See, for example, the function signature `dec(ios_base&)` ([[basefield.manip]](basefield.manip.md)).
