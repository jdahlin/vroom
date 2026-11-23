# 29.7.5.3 Unformatted output functions [ostream.unformatted]

[Contents](../../README.md) · up: [29.7.5 Output streams \[output.streams\]](output.streams.md) · prev: [29.7.5.2.4 Character inserter function templates \[ostream.inserters.character\]](ostream.inserters.character.md) · next: [29.7.5.4 Standard manipulators \[ostream.manip\]](ostream.manip.md)

**1** Each unformatted output function begins execution by constructing an object of class `sentry`. If this object returns `true`, while converting to a value of type `bool`, the function endeavors to generate the requested output. If an exception is thrown during output, then `ios_base::badbit` is turned on[^1] in `*this`’s error state. If `(exceptions() & badbit) != 0` then the exception is rethrown. In any case, the unformatted output function ends by destroying the sentry object, then, if no exception was thrown, returning the value specified for the unformatted output function.

```cpp
basic_ostream<charT, traits>& put(char_type c);
```

**2** *Effects:* Behaves as an unformatted output function (as described above). After constructing a sentry object, inserts the character `c`, if possible.[^2]

**3** Otherwise, calls `setstate(badbit)` (which may throw `ios_base::failure` ([[iostate.flags]](iostate.flags.md))).

**4** *Returns:* `*this`.

```cpp
basic_ostream& write(const char_type* s, streamsize n);
```

**5** *Effects:* Behaves as an unformatted output function (as described above). After constructing a sentry object, obtains characters to insert from successive locations of an array whose first element is designated by `s`.[^3] Characters are inserted until either of the following occurs:

- `n` characters are inserted;
- inserting in the output sequence fails (in which case the function calls `setstate(badbit)`, which may throw `ios_base::failure` ([[iostate.flags]](iostate.flags.md))).

**6** *Returns:* `*this`.

```cpp
basic_ostream& flush();
```

**7** *Effects:* Behaves as an unformatted output function (as described above). If `rdbuf()` is not a null pointer, constructs a sentry object. If this object returns `true` when converted to a value of type `bool` the function calls `rdbuf()->pubsync()`. If that function returns -1 calls `setstate(badbit)` (which may throw `ios_base::failure` ([[iostate.flags]](iostate.flags.md))). Otherwise, if the sentry object returns `false`, does nothing.

**8** *Returns:* `*this`.

[^1]: without causing an `ios_base::failure` to be thrown.
[^2]: Note that this function is not overloaded on types `signed char` and `unsigned char`.
[^3]: Note that this function is not overloaded on types `signed char` and `unsigned char`.
