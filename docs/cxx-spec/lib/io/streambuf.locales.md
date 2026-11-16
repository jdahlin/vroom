# 29.6.3.2.1 Locales [streambuf.locales]

[Contents](../../README.md) · up: [29.6.3.2 Public member functions \[streambuf.members\]](streambuf.members.md) · prev: [29.6.3.2 Public member functions \[streambuf.members\]](streambuf.members.md) · next: [29.6.3.2.2 Buffer management and positioning \[streambuf.buffer\]](streambuf.buffer.md)

```cpp
locale pubimbue(const locale& loc);
```

**1** *Effects:* Calls `imbue(loc)`.

**2** *Postconditions:* `loc == getloc()`.

**3** *Returns:* Previous value of `getloc()`.

```cpp
locale getloc() const;
```

**4** *Returns:* If `pubimbue()` has ever been called, then the last value of `loc` supplied, otherwise the current global locale, `locale()`, in effect at the time of construction. If called after `pubimbue()` has been called but before `pubimbue` has returned (i.e., from within the call of `imbue()`) then it returns the previous value.
