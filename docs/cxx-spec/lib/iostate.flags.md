# 29.5.5.4 Flags functions [iostate.flags]

[Contents](../README.md) · up: [29.5.5 Class template `basic_ios` \[ios\]](ios.md) · prev: [29.5.5.3 Member functions \[basic.ios.members\]](basic.ios.members.md) · next: [29.5.6 `ios_base` manipulators \[std.ios.manip\]](std.ios.manip.md)

```cpp
explicit operator bool() const;
```

**1** *Returns:* `!fail()`.

```cpp
bool operator!() const;
```

**2** *Returns:* `fail()`.

```cpp
iostate rdstate() const;
```

**3** *Returns:* The error state of the stream buffer.

```cpp
void clear(iostate state = goodbit);
```

**4** *Postconditions:* If `rdbuf() != 0` then `state == rdstate()`; otherwise `rdstate() == (state | ios_base::badbit)`.

**5** *Effects:* If `((state | (rdbuf() ? goodbit: badbit)) & exceptions()) == 0`, returns. Otherwise, the function throws an object of class `ios_base::failure` ([[ios.failure]](ios.failure.md)), constructed with implementation-defined argument values.

```cpp
void setstate(iostate state);
```

**6** *Effects:* Calls `clear(rdstate() | state)` (which may throw `ios_base::failure` ([[ios.failure]](ios.failure.md))).

```cpp
bool good() const;
```

**7** *Returns:* `rdstate() == 0`

```cpp
bool eof() const;
```

**8** *Returns:* `true` if `eofbit` is set in `rdstate()`.

```cpp
bool fail() const;
```

**9** *Returns:* `true` if `failbit` or `badbit` is set in `rdstate()`.[^1]

```cpp
bool bad() const;
```

**10** *Returns:* `true` if `badbit` is set in `rdstate()`.

```cpp
iostate exceptions() const;
```

**11** *Returns:* A mask that determines what elements set in `rdstate()` cause exceptions to be thrown.

```cpp
void exceptions(iostate except);
```

**12** *Postconditions:* `except == exceptions()`.

**13** *Effects:* Calls `clear(rdstate())`.

[^1]: Checking `badbit` also for `fail()` is historical practice.
