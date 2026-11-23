# 28.4.1.4.1 Members [locale.codecvt.members]

[Contents](../../README.md) · up: [28.4.1.4 Class template `codecvt` \[locale.codecvt\]](locale.codecvt.md) · prev: [28.4.1.4 Class template `codecvt` \[locale.codecvt\]](locale.codecvt.md) · next: [28.4.1.4.2 Virtual functions \[locale.codecvt.virtuals\]](locale.codecvt.virtuals.md)

```cpp
result out(
    stateT& state,
    const internT* from, const internT* from_end, const internT*& from_next,
    externT* to, externT* to_end, externT*& to_next) const;
```

**1** *Returns:* `do_out(state, from, from_end, from_next, to, to_end, to_next)`.

```cpp
result unshift(stateT& state, externT* to, externT* to_end, externT*& to_next) const;
```

**2** *Returns:* `do_unshift(state, to, to_end, to_next)`.

```cpp
result in(
    stateT& state,
    const externT* from, const externT* from_end, const externT*& from_next,
    internT* to, internT* to_end, internT*& to_next) const;
```

**3** *Returns:* `do_in(state, from, from_end, from_next, to, to_end, to_next)`.

```cpp
int encoding() const noexcept;
```

**4** *Returns:* `do_encoding()`.

```cpp
bool always_noconv() const noexcept;
```

**5** *Returns:* `do_always_noconv()`.

```cpp
int length(stateT& state, const externT* from, const externT* from_end, size_t max) const;
```

**6** *Returns:* `do_length(state, from, from_end, max)`.

```cpp
int max_length() const noexcept;
```

**7** *Returns:* `do_max_length()`.
