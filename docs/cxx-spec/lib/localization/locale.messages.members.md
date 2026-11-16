# 28.4.7.1.1 Members [locale.messages.members]

[Contents](../../README.md) · up: [28.4.7.1 Class template `messages` \[locale.messages\]](locale.messages.md) · prev: [28.4.7.1 Class template `messages` \[locale.messages\]](locale.messages.md) · next: [28.4.7.1.2 Virtual functions \[locale.messages.virtuals\]](locale.messages.virtuals.md)

```cpp
catalog open(const string& name, const locale& loc) const;
```

**1** *Returns:* `do_open(name, loc)`.

```cpp
string_type get(catalog cat, int set, int msgid, const string_type& dfault) const;
```

**2** *Returns:* `do_get(cat, set, msgid, dfault)`.

```cpp
void close(catalog cat) const;
```

**3** *Effects:* Calls `do_close(cat)`.
