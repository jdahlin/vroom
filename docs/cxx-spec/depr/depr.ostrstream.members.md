# D.12.4.2 Member functions [depr.ostrstream.members]

[Contents](../README.md) · up: [D.12.4 Class `ostrstream` \[depr.ostrstream\]](depr.ostrstream.md) · prev: [D.12.4.1 `ostrstream` constructors \[depr.ostrstream.cons\]](depr.ostrstream.cons.md) · next: [D.12.5 Class `strstream` \[depr.strstream\]](depr.strstream.md)

```cpp
strstreambuf* rdbuf() const;
```

**1** *Returns:* `(strstreambuf*)&sb`.

```cpp
void freeze(bool freezefl = true);
```

**2** *Effects:* Calls `rdbuf()->freeze(freezefl)`.

```cpp
char* str();
```

**3** *Returns:* `rdbuf()->str()`.

```cpp
int pcount() const;
```

**4** *Returns:* `rdbuf()->pcount()`.
