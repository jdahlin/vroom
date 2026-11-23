# D.12.5.1 `strstream` constructors [depr.strstream.cons]

[Contents](../README.md) · up: [D.12.5 Class `strstream` \[depr.strstream\]](depr.strstream.md) · prev: [D.12.5 Class `strstream` \[depr.strstream\]](depr.strstream.md) · next: [D.12.5.2 `strstream` destructor \[depr.strstream.dest\]](depr.strstream.dest.md)

```cpp
strstream();
```

**1** *Effects:* Initializes the base class with `iostream(&sb)`.

```cpp
strstream(char* s, int n,
          ios_base::openmode mode = ios_base::in|ios_base::out);
```

**2** *Effects:* Initializes the base class with `iostream(&sb)`, and `sb` with one of the two constructors:

- If `(mode & app) == 0`, then `s` shall designate the first element of an array of `n` elements. The constructor is `strstreambuf(s,n,s)`.
- If `(mode & app) != 0`, then `s` shall designate the first element of an array of `n` elements that contains an NTBS whose first element is designated by `s`. The constructor is `strstreambuf(s,n,s + std::strlen(s))`.
