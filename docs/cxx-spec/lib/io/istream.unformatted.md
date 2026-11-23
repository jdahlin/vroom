# 29.7.4.3 Unformatted input functions [istream.unformatted]

[Contents](../../README.md) · up: [29.7.4 Input streams \[input.streams\]](input.streams.md) · prev: [29.7.4.2.3 `basic_istream::operator>>` \[istream.extractors\]](istream.extractors.md) · next: [29.7.4.4 Standard `basic_istream` manipulators \[istream.manip\]](istream.manip.md)

**1** Each unformatted input function begins execution by constructing an object of class `sentry` with the default argument `noskipws` (second) argument `true`. If the `sentry` object returns `true`, when converted to a value of type `bool`, the function endeavors to obtain the requested input. Otherwise, if the sentry constructor exits by throwing an exception or if the sentry object returns `false`, when converted to a value of type `bool`, the function returns without attempting to obtain any input. In either case the number of extracted characters is set to 0; unformatted input functions taking a character array of nonzero size as an argument shall also store a null character (using `charT()`) in the first location of the array. If an exception is thrown during input then `ios_base::badbit` is turned on[^1] in `*this`’s error state. (Exceptions thrown from `basic_ios<>::clear()` are not caught or rethrown.) If `(exceptions()&badbit) != 0` then the exception is rethrown. It also counts the number of characters extracted. If no exception has been thrown it ends by storing the count in a member object and returning the value specified. In any event the `sentry` object is destroyed before leaving the unformatted input function.

```cpp
streamsize gcount() const;
```

**2** *Effects:* None. This member function does not behave as an unformatted input function (as described above).

**3** *Returns:* The number of characters extracted by the last unformatted input member function called for the object.

```cpp
int_type get();
```

**4** *Effects:* Behaves as an unformatted input function (as described above). After constructing a sentry object, extracts a character `c`, if one is available. Otherwise, the function calls `setstate(failbit)`, which may throw `ios_base::failure` ([[iostate.flags]](iostate.flags.md)).

**5** *Returns:* `c` if available, otherwise `traits::eof()`.

```cpp
basic_istream<charT, traits>& get(char_type& c);
```

**6** *Effects:* Behaves as an unformatted input function (as described above). After constructing a sentry object, extracts a character, if one is available, and assigns it to `c`.[^2] Otherwise, the function calls `setstate(failbit)` (which may throw `ios_base::failure` ([[iostate.flags]](iostate.flags.md))).

**7** *Returns:* `*this`.

```cpp
basic_istream<charT, traits>& get(char_type* s, streamsize n, char_type delim);
```

**8** *Effects:* Behaves as an unformatted input function (as described above). After constructing a sentry object, extracts characters and stores them into successive locations of an array whose first element is designated by `s`.[^3] Characters are extracted and stored until any of the following occurs:

- `n` is less than one or `n - 1` characters are stored;
- end-of-file occurs on the input sequence (in which case the function calls `setstate(eofbit)`);
- `traits::eq(c, delim)` for the next available input character `c` (in which case `c` is not extracted).

**9** If the function stores no characters, it calls `setstate(failbit)` (which may throw `ios_base::failure` ([[iostate.flags]](iostate.flags.md))). In any case, if `n` is greater than zero it then stores a null character into the next successive location of the array.

**10** *Returns:* `*this`.

```cpp
basic_istream<charT, traits>& get(char_type* s, streamsize n);
```

**11** *Effects:* Calls `get(s, n, widen('\n'))`.

**12** *Returns:* Value returned by the call.

```cpp
basic_istream<charT, traits>& get(basic_streambuf<char_type, traits>& sb, char_type delim);
```

**13** *Effects:* Behaves as an unformatted input function (as described above). After constructing a sentry object, extracts characters and inserts them in the output sequence controlled by `sb`. Characters are extracted and inserted until any of the following occurs:

- end-of-file occurs on the input sequence;
- inserting in the output sequence fails (in which case the character to be inserted is not extracted);
- `traits::eq(c, delim)` for the next available input character `c` (in which case `c` is not extracted);
- an exception occurs (in which case, the exception is caught but not rethrown).

**14** If the function inserts no characters, it calls `setstate(failbit)`, which may throw `ios_base::failure` ([[iostate.flags]](iostate.flags.md)).

**15** *Returns:* `*this`.

```cpp
basic_istream<charT, traits>& get(basic_streambuf<char_type, traits>& sb);
```

**16** *Effects:* Calls `get(sb, widen('\n'))`.

**17** *Returns:* Value returned by the call.

```cpp
basic_istream<charT, traits>& getline(char_type* s, streamsize n, char_type delim);
```

**18** *Effects:* Behaves as an unformatted input function (as described above). After constructing a sentry object, extracts characters and stores them into successive locations of an array whose first element is designated by `s`.[^4] Characters are extracted and stored until one of the following occurs:

1. end-of-file occurs on the input sequence (in which case the function calls `setstate(eofbit)`);
2. `traits::eq(c, delim)` for the next available input character `c` (in which case the input character is extracted but not stored);[^5]
3. `n` is less than one or `n - 1` characters are stored (in which case the function calls `setstate(failbit)`).

**19** These conditions are tested in the order shown.[^6]

**20** If the function extracts no characters, it calls `setstate(failbit)` (which may throw `ios_base::failure` ([[iostate.flags]](iostate.flags.md))).[^7]

**21** In any case, if `n` is greater than zero, it then stores a null character (using `charT()`) into the next successive location of the array.

**22** *Returns:* `*this`.

**23** [*Example 1*:

```cpp
#include <iostream>

int main() {
  using namespace std;
  const int line_buffer_size = 100;

  char buffer[line_buffer_size];
  int line_number = 0;
  while (cin.getline(buffer, line_buffer_size, '\n') || cin.gcount()) {
    int count = cin.gcount();
    if (cin.eof())
      cout << "Partial final line";     // cin.fail() is false
    else if (cin.fail()) {
      cout << "Partial long line";
      cin.clear(cin.rdstate() & ~ios_base::failbit);
    } else {
      count--;                          // Don't include newline in count
      cout << "Line " << ++line_number;
    }
    cout << " (" << count << " chars): " << buffer << endl;
  }
}
```

— *end example*]

```cpp
basic_istream<charT, traits>& getline(char_type* s, streamsize n);
```

**24** *Returns:* `getline(s, n, widen('\n'))`

```cpp
basic_istream<charT, traits>& ignore(streamsize n = 1, int_type delim = traits::eof());
```

**25** *Effects:* Behaves as an unformatted input function (as described above). After constructing a sentry object, extracts characters and discards them. Characters are extracted until any of the following occurs:

- `n != numeric_limits<streamsize>::max()` ([[numeric.limits]](../support/numeric.limits.md)) and `n` characters have been extracted so far
- end-of-file occurs on the input sequence (in which case the function calls `setstate(eofbit)`, which may throw `ios_base::failure` ([[iostate.flags]](iostate.flags.md)));
- `traits::eq_int_type(traits::to_int_type(c), delim)` for the next available input character `c` (in which case `c` is extracted).

**26** *Remarks:* The last condition will never occur if `traits::eq_int_type(delim, traits::eof())`.

**27** *Returns:* `*this`.

```cpp
int_type peek();
```

**28** *Effects:* Behaves as an unformatted input function (as described above). After constructing a sentry object, reads but does not extract the current input character.

**29** *Returns:* `traits::eof()` if `good()` is `false`. Otherwise, returns `rdbuf()->sgetc()`.

```cpp
basic_istream<charT, traits>& read(char_type* s, streamsize n);
```

**30** *Effects:* Behaves as an unformatted input function (as described above). After constructing a sentry object, if `!good()` calls `setstate(failbit)` which may throw an exception, and return. Otherwise extracts characters and stores them into successive locations of an array whose first element is designated by `s`.[^8] Characters are extracted and stored until either of the following occurs:

- `n` characters are stored;
- end-of-file occurs on the input sequence (in which case the function calls `setstate(failbit | eofbit)`, which may throw `ios_base::failure` ([[iostate.flags]](iostate.flags.md))).

**31** *Returns:* `*this`.

```cpp
streamsize readsome(char_type* s, streamsize n);
```

**32** *Effects:* Behaves as an unformatted input function (as described above). After constructing a sentry object, if `!good()` calls `setstate(failbit)` which may throw an exception, and return. Otherwise extracts characters and stores them into successive locations of an array whose first element is designated by `s`. If `rdbuf()->in_avail() == -1`, calls `setstate(eofbit)` (which may throw `ios_base::failure` ([[iostate.flags]](iostate.flags.md))), and extracts no characters;

- If `rdbuf()->in_avail() == 0`, extracts no characters
- If `rdbuf()->in_avail() > 0`, extracts `min(rdbuf()->in_avail(), n))`.

**33** *Returns:* The number of characters extracted.

```cpp
basic_istream<charT, traits>& putback(char_type c);
```

**34** *Effects:* Behaves as an unformatted input function (as described above), except that the function first clears `eofbit`. After constructing a sentry object, if `!good()` calls `setstate(failbit)` which may throw an exception, and return. If `rdbuf()` is not null, calls `rdbuf()->sputbackc(c)`. If `rdbuf()` is null, or if `sputbackc` returns `traits::eof()`, calls `setstate(badbit)` (which may throw `ios_base::failure` ([[iostate.flags]](iostate.flags.md))). [*Note 1*: This function extracts no characters, so the value returned by the next call to `gcount()` is 0. — *end note*]

**35** *Returns:* `*this`.

```cpp
basic_istream<charT, traits>& unget();
```

**36** *Effects:* Behaves as an unformatted input function (as described above), except that the function first clears `eofbit`. After constructing a sentry object, if `!good()` calls `setstate(failbit)` which may throw an exception, and return. If `rdbuf()` is not null, calls `rdbuf()->sungetc()`. If `rdbuf()` is null, or if `sungetc` returns `traits::eof()`, calls `setstate(badbit)` (which may throw `ios_base::failure` ([[iostate.flags]](iostate.flags.md))). [*Note 2*: This function extracts no characters, so the value returned by the next call to `gcount()` is 0. — *end note*]

**37** *Returns:* `*this`.

```cpp
int sync();
```

**38** *Effects:* Behaves as an unformatted input function (as described above), except that it does not count the number of characters extracted and does not affect the value returned by subsequent calls to `gcount()`. After constructing a sentry object, if `rdbuf()` is a null pointer, returns `-1`. Otherwise, calls `rdbuf()->pubsync()` and, if that function returns `-1` calls `setstate(badbit)` (which may throw `ios_base::failure` ([[iostate.flags]](iostate.flags.md)), and returns `-1`. Otherwise, returns zero.

```cpp
pos_type tellg();
```

**39** *Effects:* Behaves as an unformatted input function (as described above), except that it does not count the number of characters extracted and does not affect the value returned by subsequent calls to `gcount()`.

**40** *Returns:* After constructing a sentry object, if `fail() != false`, returns `pos_type(-1)` to indicate failure. Otherwise, returns `rdbuf()->pubseekoff(0, cur, in)`.

```cpp
basic_istream<charT, traits>& seekg(pos_type pos);
```

**41** *Effects:* Behaves as an unformatted input function (as described above), except that the function first clears `eofbit`, it does not count the number of characters extracted, and it does not affect the value returned by subsequent calls to `gcount()`. After constructing a sentry object, if `fail() != true`, executes `rdbuf()->pubseekpos(pos, ios_base::in)`. In case of failure, the function calls `setstate(failbit)` (which may throw `ios_base::failure`).

**42** *Returns:* `*this`.

```cpp
basic_istream<charT, traits>& seekg(off_type off, ios_base::seekdir dir);
```

**43** *Effects:* Behaves as an unformatted input function (as described above), except that the function first clears `eofbit`, does not count the number of characters extracted, and does not affect the value returned by subsequent calls to `gcount()`. After constructing a sentry object, if `fail() != true`, executes `rdbuf()->pubseekoff(off, dir, ios_base::in)`. In case of failure, the function calls `setstate(failbit)` (which may throw `ios_base::failure`).

**44** *Returns:* `*this`.

[^1]: This is done without causing an `ios_base::failure` to be thrown.
[^2]: Note that this function is not overloaded on types `signed char` and `unsigned char`.
[^3]: Note that this function is not overloaded on types `signed char` and `unsigned char`.
[^4]: Note that this function is not overloaded on types `signed char` and `unsigned char`.
[^5]: Since the final input character is “extracted”, it is counted in the `gcount()`, even though it is not stored.
[^6]: This allows an input line which exactly fills the buffer, without setting `failbit`. This is different behavior than the historical AT&T implementation.
[^7]: This implies an empty input line will not cause `failbit` to be set.
[^8]: Note that this function is not overloaded on types `signed char` and `unsigned char`.
