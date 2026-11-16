# 29.5.3.3 Functions [ios.base.locales]

[Contents](../README.md) · up: [29.5.3 Class `ios_base` \[ios.base\]](ios.base.md) · prev: [29.5.3.2 State functions \[fmtflags.state\]](fmtflags.state.md) · next: [29.5.3.4 Static members \[ios.members.static\]](ios.members.static.md)

```cpp
locale imbue(const locale& loc);
```

**1** *Effects:* Calls each registered callback pair `(fn, idx)` ([[ios.base.callback]](ios.base.callback.md)) as `(*fn)(imbue_event, *this, idx)` at such a time that a call to `ios_base::getloc()` from within `fn` returns the new locale value `loc`.

**2** *Postconditions:* `loc == getloc()`.

**3** *Returns:* The previous value of `getloc()`.

```cpp
locale getloc() const;
```

**4** *Returns:* If no locale has been imbued, a copy of the global C++ locale, `locale()`, in effect at the time of construction. Otherwise, returns the imbued locale, to be used to perform locale-dependent input and output operations.
