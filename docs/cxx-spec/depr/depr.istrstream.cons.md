# D.12.3.1 `istrstream` constructors [depr.istrstream.cons]

[Contents](../README.md) · up: [D.12.3 Class `istrstream` \[depr.istrstream\]](depr.istrstream.md) · prev: [D.12.3 Class `istrstream` \[depr.istrstream\]](depr.istrstream.md) · next: [D.12.3.2 Member functions \[depr.istrstream.members\]](depr.istrstream.members.md)

```cpp
explicit istrstream(const char* s);
explicit istrstream(char* s);
```

**1** *Effects:* Initializes the base class with `istream(&sb)` and `sb` with `strstreambuf(s, 0)`. `s` shall designate the first element of an NTBS.

```cpp
istrstream(const char* s, streamsize n);
istrstream(char* s, streamsize n);
```

**2** *Effects:* Initializes the base class with `istream(&sb)` and `sb` with `strstreambuf(s, n)`. `s` shall designate the first element of an array whose length is `n` elements, and `n` shall be greater than zero.
