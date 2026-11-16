# D.12.5.3 `strstream` operations [depr.strstream.oper]

[Contents](../README.md) · up: [D.12.5 Class `strstream` \[depr.strstream\]](depr.strstream.md) · prev: [D.12.5.2 `strstream` destructor \[depr.strstream.dest\]](depr.strstream.dest.md) · next: [D.13 Deprecated type traits \[depr.meta.types\]](depr.meta.types.md)

```cpp
strstreambuf* rdbuf() const;
```

**1** *Returns:* `const_cast<strstreambuf*>(&sb)`.

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
