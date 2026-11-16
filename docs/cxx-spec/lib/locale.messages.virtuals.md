# 28.4.7.1.2 Virtual functions [locale.messages.virtuals]

[Contents](../README.md) · up: [28.4.7.1 Class template `messages` \[locale.messages\]](locale.messages.md) · prev: [28.4.7.1.1 Members \[locale.messages.members\]](locale.messages.members.md) · next: [28.4.7.2 Class template `messages_byname` \[locale.messages.byname\]](locale.messages.byname.md)

```cpp
catalog do_open(const string& name, const locale& loc) const;
```

**1** *Returns:* A value that may be passed to `get()` to retrieve a message from the message catalog identified by the string `name` according to an implementation-defined mapping. The result can be used until it is passed to `close()`.

**2** Returns a value less than 0 if no such catalog can be opened.

**3** *Remarks:* The locale argument `loc` is used for character set code conversion when retrieving messages, if needed.

```cpp
string_type do_get(catalog cat, int set, int msgid, const string_type& dfault) const;
```

**4** *Preconditions:* `cat` is a catalog obtained from `open()` and not yet closed.

**5** *Returns:* A message identified by arguments `set`, `msgid`, and `dfault`, according to an implementation-defined mapping. If no such message can be found, returns `dfault`.

```cpp
void do_close(catalog cat) const;
```

**6** *Preconditions:* `cat` is a catalog obtained from `open()` and not yet closed.

**7** *Effects:* Releases unspecified resources associated with `cat`.

**8** *Remarks:* The limit on such resources, if any, is implementation-defined.
