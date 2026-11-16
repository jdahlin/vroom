# D.12.4.1 `ostrstream` constructors [depr.ostrstream.cons]

[Contents](README.md) · up: [D.12.4 Class `ostrstream` \[depr.ostrstream\]](depr.ostrstream.md) · prev: [D.12.4 Class `ostrstream` \[depr.ostrstream\]](depr.ostrstream.md) · next: [D.12.4.2 Member functions \[depr.ostrstream.members\]](depr.ostrstream.members.md)

```cpp
ostrstream();
```

**1** *Effects:* Initializes the base class with `ostream(&sb)` and `sb` with `strstreambuf()`.

```cpp
ostrstream(char* s, int n, ios_base::openmode mode = ios_base::out);
```

**2** *Effects:* Initializes the base class with `ostream(&sb)`, and `sb` with one of two constructors:

- If `(mode & app) == 0`, then `s` shall designate the first element of an array of `n` elements.

  The constructor is `strstreambuf(s, n, s)`.

- If `(mode & app) != 0`, then `s` shall designate the first element of an array of `n` elements that contains an NTBS whose first element is designated by `s`. The constructor is `strstreambuf(s, n, s + std::strlen(s))`.[^1]

[^1]: The function signature `strlen(const char*)` is declared in `<cstring>` ([[cstring.syn]](lib/cstring.syn.md)).
