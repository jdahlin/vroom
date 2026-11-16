# D.12.3.2 Member functions [depr.istrstream.members]

[Contents](README.md) · up: [D.12.3 Class `istrstream` \[depr.istrstream\]](depr.istrstream.md) · prev: [D.12.3.1 `istrstream` constructors \[depr.istrstream.cons\]](depr.istrstream.cons.md) · next: [D.12.4 Class `ostrstream` \[depr.ostrstream\]](depr.ostrstream.md)

```cpp
strstreambuf* rdbuf() const;
```

**1** *Returns:* `const_cast<strstreambuf*>(&sb)`.

```cpp
char* str();
```

**2** *Returns:* `rdbuf()->str()`.
